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

#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_sorts.h"
#include "svn_pools.h"


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
  apr_hash_t *paths;
  svn_revnum_t target_revision;
  apr_pool_t *edit_pool;
  svn_delta_fetch_props_cb_func_t fetch_props_func;
  void *fetch_props_baton;
};

struct ev2_dir_baton
{
  struct ev2_edit_baton *eb;
  const char *path;
};

struct ev2_file_baton
{
  struct ev2_edit_baton *eb;
  const char *path;
};

enum action
{
  add,
  delete,
  set_prop,
  remove_prop
};

struct path_action
{
  enum action action;
  void *args;
};

struct prop_args
{
  const char *name;
  const svn_string_t *value;
};

static svn_error_t *
add_action(struct ev2_edit_baton *eb,
           const char *path,
           enum action action,
           void *args)
{
  struct path_action *p_action;
  apr_array_header_t *action_list = apr_hash_get(eb->paths, path,
                                                 APR_HASH_KEY_STRING);

  p_action = apr_palloc(eb->edit_pool, sizeof(*p_action));
  p_action->action = action;
  p_action->args = args;

  if (action_list == NULL)
    {
      action_list = apr_array_make(eb->edit_pool, 1,
                                   sizeof(struct path_action *));
      apr_hash_set(eb->paths, apr_pstrdup(eb->edit_pool, path),
                   APR_HASH_KEY_STRING, action_list);
    }

  APR_ARRAY_PUSH(action_list, struct path_action *) = p_action;

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_set_target_revision(void *edit_baton,
                        svn_revnum_t target_revision,
                        apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;

  eb->target_revision = target_revision;
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
  db->path = "";

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
  svn_revnum_t *revnum = apr_palloc(pb->eb->edit_pool, sizeof(*revnum));

  *revnum = revision;
  SVN_ERR(add_action(pb->eb, path, delete, revnum));

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
  struct ev2_dir_baton *cb = apr_palloc(result_pool, sizeof(*cb));
  svn_node_kind_t *kind;

  kind = apr_palloc(pb->eb->edit_pool, sizeof(*kind));
  *kind = svn_node_dir;
  SVN_ERR(add_action(pb->eb, path, add, kind));

  cb->eb = pb->eb;
  cb->path = apr_pstrdup(result_pool, path);
  *child_baton = cb;

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
  db->path = apr_pstrdup(result_pool, path);

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

  if (value)
    {
      struct prop_args *p_args = apr_palloc(db->eb->edit_pool, sizeof(*p_args));

      p_args->name = apr_pstrdup(db->eb->edit_pool, name);
      p_args->value = svn_string_dup(value, db->eb->edit_pool);

      SVN_ERR(add_action(db->eb, db->path, set_prop, p_args));
    }
  else
    SVN_ERR(add_action(db->eb, db->path, remove_prop,
                       apr_pstrdup(db->eb->edit_pool, name)));

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
  svn_node_kind_t *kind;

  fb->eb = pb->eb;
  fb->path = apr_pstrdup(result_pool, path);
  *file_baton = fb;

  kind = apr_palloc(pb->eb->edit_pool, sizeof(*kind));
  *kind = svn_node_file;
  SVN_ERR(add_action(pb->eb, path, add, kind));

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
  fb->path = apr_pstrdup(result_pool, path);

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

  if (value)
    {
      struct prop_args *p_args = apr_palloc(fb->eb->edit_pool, sizeof(*p_args));
      p_args->name = apr_pstrdup(fb->eb->edit_pool, name);
      p_args->value = svn_string_dup(value, fb->eb->edit_pool);

      SVN_ERR(add_action(fb->eb, fb->path, set_prop, p_args));
    }
  else
    SVN_ERR(add_action(fb->eb, fb->path, remove_prop,
                       apr_pstrdup(fb->eb->edit_pool, name)));

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
process_actions(void *edit_baton,
                const char *path,
                apr_array_header_t *actions,
                apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;
  apr_array_header_t *removed_props = NULL;
  apr_hash_t *props = NULL;
  int i;

  /* Go through all of our actions, populating various datastructures
   * dependent on them. */
  for (i = 0; i < actions->nelts; i++)
    {
      struct path_action *action = APR_ARRAY_IDX(actions, i,
                                                 struct path_action *);

      switch (action->action)
        {
          case set_prop:
            {
              struct prop_args *p_args = action->args;

              if (!props)
                props = apr_hash_make(scratch_pool);

              apr_hash_set(props, p_args->name, APR_HASH_KEY_STRING,
                           p_args->value);
              break;
            }

          case remove_prop:
            {
              const char *name = action->args;

              if (!removed_props)
                removed_props = apr_array_make(scratch_pool, 1,
                                               sizeof(const char *));

              APR_ARRAY_PUSH(removed_props, const char *) = name;
              break;
            }

          case delete:
            {
              svn_revnum_t *revnum = action->args;

              /* If we get a delete, we'd better not have gotten any
                 other actions for this path later, so we can go ahead
                 and call our handler. */
              SVN_ERR(svn_editor_delete(eb->editor, path, *revnum));
              break;
            }

          default:
            break;
        }
    }

  /* We've now got a wholistic view of what has happened to this node,
   * so we can call our own editor APIs on it. */

  if (props || removed_props)
    {
      /* If we've seen any prop mods, we're going to need to fetch the
         existing props so that we can properly drive the editor method
         below. */
      apr_hash_t *existing_props;

      SVN_ERR(eb->fetch_props_func(&existing_props, eb->fetch_props_baton,
                                   path, scratch_pool));
      if (props)
        props = apr_hash_overlay(scratch_pool, props, existing_props);
      else
        props = existing_props;

      /* Now delete any props which were deleted in our drive. */
      if (removed_props)
        {
          for (i = 0; i < removed_props->nelts; i++)
            {
              const char *name = APR_ARRAY_IDX(removed_props, i,
                                               const char *);
              apr_hash_set(props, name, APR_HASH_KEY_STRING, NULL);
            }
        }

      /* This point, PROPS will contain all the props for this node. */
      SVN_ERR(svn_editor_set_props(eb->editor, path, eb->target_revision,
                                   props, TRUE));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_close_edit(void *edit_baton,
               apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;
  apr_array_header_t *sorted_hash;
  apr_pool_t *iterpool;
  int i;

  /* Sort the paths touched by this edit.
   * Ev2 doesn't really have any particular need for depth-first-ness, but
   * we want to ensure all parent directories are handled before children in
   * the case of adds (which does introduce an element of depth-first-ness). */
  sorted_hash = svn_sort__hash(eb->paths, svn_sort_compare_items_as_paths,
                               scratch_pool);

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < sorted_hash->nelts; i++)
    {
      svn_sort__item_t *item = &APR_ARRAY_IDX(sorted_hash, i, svn_sort__item_t);
      apr_array_header_t *actions = item->value;
      const char *path = item->key;

      svn_pool_clear(iterpool);
      SVN_ERR(process_actions(edit_baton, path, actions, iterpool));
    }
  svn_pool_destroy(iterpool);

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
svn_delta_from_editor(const svn_delta_editor_t **deditor,
                      void **dedit_baton,
                      svn_editor_t *editor,
                      svn_delta_fetch_props_cb_func_t fetch_props_func,
                      void *fetch_props_baton,
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
  eb->paths = apr_hash_make(pool);
  eb->target_revision = SVN_INVALID_REVNUM;
  eb->edit_pool = pool;
  eb->fetch_props_func = fetch_props_func;
  eb->fetch_props_baton = fetch_props_baton;

  *dedit_baton = eb;
  *deditor = &delta_editor;

  return SVN_NO_ERROR;
}


struct editor_baton
{
  const svn_delta_editor_t *deditor;
  void *dedit_baton;
};

/* This implements svn_editor_cb_add_directory_t */
static svn_error_t *
add_directory_cb(void *baton,
                 const char *relpath,
                 const apr_array_header_t *children,
                 apr_hash_t *props,
                 svn_revnum_t replaces_rev,
                 apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_add_file_t */
static svn_error_t *
add_file_cb(void *baton,
            const char *relpath,
            apr_hash_t *props,
            svn_revnum_t replaces_rev,
            apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_add_symlink_t */
static svn_error_t *
add_symlink_cb(void *baton,
               const char *relpath,
               const char *target,
               apr_hash_t *props,
               svn_revnum_t replaces_rev,
               apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_add_absent_t */
static svn_error_t *
add_absent_cb(void *baton,
              const char *relpath,
              svn_node_kind_t kind,
              svn_revnum_t replaces_rev,
              apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_set_props_t */
static svn_error_t *
set_props_cb(void *baton,
             const char *relpath,
             svn_revnum_t revision,
             apr_hash_t *props,
             svn_boolean_t complete,
             apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_set_text_t */
static svn_error_t *
set_text_cb(void *baton,
            const char *relpath,
            svn_revnum_t revision,
            const svn_checksum_t *checksum,
            svn_stream_t *contents,
            apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_set_target_t */
static svn_error_t *
set_target_cb(void *baton,
              const char *relpath,
              svn_revnum_t revision,
              const char *target,
              apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_delete_t */
static svn_error_t *
delete_cb(void *baton,
          const char *relpath,
          svn_revnum_t revision,
          apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_copy_t */
static svn_error_t *
copy_cb(void *baton,
        const char *src_relpath,
        svn_revnum_t src_revision,
        const char *dst_relpath,
        svn_revnum_t replaces_rev,
        apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_move_t */
static svn_error_t *
move_cb(void *baton,
        const char *src_relpath,
        svn_revnum_t src_revision,
        const char *dst_relpath,
        svn_revnum_t replaces_rev,
        apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_complete_t */
static svn_error_t *
complete_cb(void *baton,
            apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;
  return svn_error_trace(eb->deditor->close_edit(eb->dedit_baton,
                                                 scratch_pool));
}

/* This implements svn_editor_cb_abort_t */
static svn_error_t *
abort_cb(void *baton,
         apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;
  return svn_error_trace(eb->deditor->abort_edit(eb->dedit_baton,
                                                 scratch_pool));
}

svn_error_t *
svn_editor_from_delta(svn_editor_t **editor_p,
                      const svn_delta_editor_t *deditor,
                      void *dedit_baton,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_editor_t *editor;
  static const svn_editor_cb_many_t editor_cbs = {
      add_directory_cb,
      add_file_cb,
      add_symlink_cb,
      add_absent_cb,
      set_props_cb,
      set_text_cb,
      set_target_cb,
      delete_cb,
      copy_cb,
      move_cb,
      complete_cb,
      abort_cb
    };
  struct editor_baton *eb = apr_palloc(result_pool, sizeof(*eb));

  eb->deditor = deditor;
  eb->dedit_baton = dedit_baton;

  SVN_ERR(svn_editor_create(&editor, eb, cancel_func, cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(editor, &editor_cbs, scratch_pool));

  *editor_p = editor;

  return SVN_NO_ERROR;
}


/* Uncomment below to add editor shims throughout Subversion.  In it's
 * current state, that will likely break The World. */
/* #define ENABLE_EDITOR_SHIMS*/

svn_error_t *
svn_editor__insert_shims(const svn_delta_editor_t **deditor_out,
                         void **dedit_baton_out,
                         const svn_delta_editor_t *deditor_in,
                         void *dedit_baton_in,
                         svn_delta_fetch_props_cb_func_t fetch_props_func,
                         void *fetch_props_baton,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
#ifndef ENABLE_EDITOR_SHIMS
  /* Shims disabled, just copy the editor and baton directly. */
  *deditor_out = deditor_in;
  *dedit_baton_out = dedit_baton_in;
#else
  /* Use our shim APIs to create an intermediate svn_editor_t, and then
     wrap that again back into a svn_delta_editor_t.  This introduces
     a lot of overhead. */
  svn_editor_t *editor;

  SVN_ERR(svn_editor_from_delta(&editor, deditor_in, dedit_baton_in,
                                NULL, NULL, result_pool, scratch_pool));
  SVN_ERR(svn_delta_from_editor(deditor_out, dedit_baton_out, editor,
                                fetch_props_func, fetch_props_baton,
                                result_pool));

#endif
  return SVN_NO_ERROR;
}
