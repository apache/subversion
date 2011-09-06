/*
 * compat.c :  Wrappers and callbacks for compatibility.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"


struct file_rev_handler_wrapper_baton {
  void *baton;
  svn_file_rev_handler_old_t handler;
};

/* This implements svn_file_rev_handler_t. */
static svn_error_t *
file_rev_handler_wrapper(void *baton,
                         const char *path,
                         svn_revnum_t rev,
                         apr_hash_t *rev_props,
                         svn_boolean_t result_of_merge,
                         svn_txdelta_window_handler_t *delta_handler,
                         void **delta_baton,
                         apr_array_header_t *prop_diffs,
                         apr_pool_t *pool)
{
  struct file_rev_handler_wrapper_baton *fwb = baton;

  if (fwb->handler)
    return fwb->handler(fwb->baton,
                        path,
                        rev,
                        rev_props,
                        delta_handler,
                        delta_baton,
                        prop_diffs,
                        pool);

  return SVN_NO_ERROR;
}

void
svn_compat_wrap_file_rev_handler(svn_file_rev_handler_t *handler2,
                                 void **handler2_baton,
                                 svn_file_rev_handler_old_t handler,
                                 void *handler_baton,
                                 apr_pool_t *pool)
{
  struct file_rev_handler_wrapper_baton *fwb = apr_palloc(pool, sizeof(*fwb));

  /* Set the user provided old format callback in the baton. */
  fwb->baton = handler_baton;
  fwb->handler = handler;

  *handler2_baton = fwb;
  *handler2 = file_rev_handler_wrapper;
}


/* The following code maps the calls to a traditional delta editor to an
 * Editorv2 editor.  It does this by keeping track of a lot of state, and
 * then communicating that state to Ev2 upon closure of the file or dir (or
 * edit).  Note that Ev2 calls add_symlink() and set_target() are not present
 * in the delta editor paradigm, so we never call them.
 *
 * The general idea here is that we have to see *all* the actions on a node's
 * parent before we can process that node, which means we need to buffer a
 * large amount of information in the dir batons, and then process it in the
 * close_directory() handler. */

struct ev2_edit_baton
{
  svn_editor_t *editor;
};

struct ev2_dir_baton
{
  struct ev2_edit_baton *eb;
};

struct ev2_file_baton
{
  struct ev2_edit_baton *eb;
};

static svn_error_t *
ev2_set_target_revision(void *edit_baton,
                        svn_revnum_t target_revision,
                        apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_open_root(void *edit_baton,
              svn_revnum_t base_revision,
              apr_pool_t *result_pool,
              void **root_baton)
{
  struct ev2_dir_baton *db = apr_palloc(result_pool, sizeof(*db));
  struct ev2_edit_baton *eb = edit_baton;

  db->eb = eb;

  *root_baton = db;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_delete_entry(const char *path,
                 svn_revnum_t revision,
                 void *parent_baton,
                 apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *pb = parent_baton;

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_add_directory(const char *path,
                  void *parent_baton,
                  const char *copyfrom_path,
                  svn_revnum_t copyfrom_revision,
                  apr_pool_t *result_pool,
                  void **child_baton)
{
  struct ev2_dir_baton *pb = parent_baton;

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_open_directory(const char *path,
                   void *parent_baton,
                   svn_revnum_t base_revision,
                   apr_pool_t *result_pool,
                   void **child_baton)
{
  struct ev2_dir_baton *pb = parent_baton;
  struct ev2_dir_baton *db = apr_palloc(result_pool, sizeof(*db));

  db->eb = pb->eb;

  *child_baton = db;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_change_dir_prop(void *dir_baton,
                    const char *name,
                    const svn_string_t *value,
                    apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *db = dir_baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_close_directory(void *dir_baton,
                    apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *db = dir_baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_absent_directory(const char *path,
                     void *parent_baton,
                     apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *pb = parent_baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_add_file(const char *path,
             void *parent_baton,
             const char *copyfrom_path,
             svn_revnum_t copyfrom_revision,
             apr_pool_t *result_pool,
             void **file_baton)
{
  struct ev2_file_baton *fb = apr_palloc(result_pool, sizeof(*fb));
  struct ev2_dir_baton *pb = parent_baton;

  fb->eb = pb->eb;

  *file_baton = fb;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_open_file(const char *path,
              void *parent_baton,
              svn_revnum_t base_revision,
              apr_pool_t *result_pool,
              void **file_baton)
{
  struct ev2_file_baton *fb = apr_palloc(result_pool, sizeof(*fb));
  struct ev2_dir_baton *pb = parent_baton;

  fb->eb = pb->eb;

  *file_baton = fb;
  return SVN_NO_ERROR;
}


static svn_error_t *
ev2_apply_textdelta(void *file_baton,
                    const char *base_checksum,
                    apr_pool_t *result_pool,
                    svn_txdelta_window_handler_t *handler,
                    void **handler_baton)
{
  struct ev2_file_baton *fb = file_baton;

  *handler_baton = NULL;
  *handler = svn_delta_noop_window_handler;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_change_file_prop(void *file_baton,
                     const char *name,
                     const svn_string_t *value,
                     apr_pool_t *scratch_pool)
{
  struct ev2_file_baton *fb = file_baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_close_file(void *file_baton,
               const char *text_checksum,
               apr_pool_t *scratch_pool)
{
  struct ev2_file_baton *fb = file_baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_absent_file(const char *path,
                void *parent_baton,
                apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *pb = parent_baton;
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_close_edit(void *edit_baton,
               apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;

  return svn_error_trace(svn_editor_complete(eb->editor));
}

static svn_error_t *
ev2_abort_edit(void *edit_baton,
               apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;

  return svn_error_trace(svn_editor_abort(eb->editor));
}

svn_error_t *
svn_delta_from_editor(svn_delta_editor_t **deditor,
                      void **dedit_baton,
                      svn_editor_t *editor,
                      apr_pool_t *pool)
{
  /* Static 'cause we don't want it to be on the stack. */
  static svn_delta_editor_t delta_editor = {
      ev2_set_target_revision,
      ev2_open_root,
      ev2_delete_entry,
      ev2_add_directory,
      ev2_open_directory,
      ev2_change_dir_prop,
      ev2_close_directory,
      ev2_absent_directory,
      ev2_add_file,
      ev2_open_file,
      ev2_apply_textdelta,
      ev2_change_file_prop,
      ev2_close_file,
      ev2_absent_file,
      ev2_close_edit,
      ev2_abort_edit
    };
  struct ev2_edit_baton *eb = apr_palloc(pool, sizeof(*eb));

  eb->editor = editor;

  *dedit_baton = eb;
  *deditor = &delta_editor;

  return SVN_NO_ERROR;
}
