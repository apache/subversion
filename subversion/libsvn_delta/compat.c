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
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"
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
  svn_delta_fetch_props_func_t fetch_props_func;
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
  ACTION_ADD,
  ACTION_DELETE,
  ACTION_SET_PROP
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
process_actions(void *edit_baton,
                const char *path,
                apr_array_header_t *actions,
                apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;
  apr_hash_t *props = NULL;
  int i;

  /* Go through all of our actions, populating various datastructures
   * dependent on them. */
  for (i = 0; i < actions->nelts; i++)
    {
      const struct path_action *action = APR_ARRAY_IDX(actions, i,
                                                       struct path_action *);

      switch (action->action)
        {
          case ACTION_SET_PROP:
            {
              const struct prop_args *p_args = action->args;

              if (!props)
                {
                  /* Fetch the original props. We can then apply each of
                     the modifications to it.  */
                  SVN_ERR(eb->fetch_props_func(&props,
                                               eb->fetch_props_baton,
                                               path,
                                               scratch_pool, scratch_pool));
                }

              /* Note that p_args->value may be NULL.  */
              apr_hash_set(props, p_args->name, APR_HASH_KEY_STRING,
                           p_args->value);
              break;
            }

          case ACTION_DELETE:
            {
              svn_revnum_t *revnum = action->args;

              /* If we get a delete, we'd better not have gotten any
                 other actions for this path later, so we can go ahead
                 and call our handler. */
              SVN_ERR(svn_editor_delete(eb->editor, path, *revnum));
              break;
            }

          case ACTION_ADD:
            {
              /* ### do something  */
              break;
            }

          default:
            SVN_ERR_MALFUNCTION();
        }
    }

  /* We've now got a wholistic view of what has happened to this node,
   * so we can call our own editor APIs on it. */

  if (props)
    {
      /* We fetched and modified the props in some way. Apply 'em now that
         we have the new set.  */
      SVN_ERR(svn_editor_set_props(eb->editor, path, eb->target_revision,
                                   props, TRUE));
    }

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
  SVN_ERR(add_action(pb->eb, path, ACTION_DELETE, revnum));

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
  svn_kind_t *kind;

  kind = apr_palloc(pb->eb->edit_pool, sizeof(*kind));
  *kind = svn_node_dir;
  SVN_ERR(add_action(pb->eb, path, ACTION_ADD, kind));

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
  struct prop_args *p_args = apr_palloc(db->eb->edit_pool, sizeof(*p_args));

  p_args->name = apr_pstrdup(db->eb->edit_pool, name);
  p_args->value = value ? svn_string_dup(value, db->eb->edit_pool) : NULL;

  SVN_ERR(add_action(db->eb, db->path, ACTION_SET_PROP, p_args));

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
  svn_kind_t *kind;

  fb->eb = pb->eb;
  fb->path = apr_pstrdup(result_pool, path);
  *file_baton = fb;

  kind = apr_palloc(pb->eb->edit_pool, sizeof(*kind));
  *kind = svn_node_file;
  SVN_ERR(add_action(pb->eb, path, ACTION_ADD, kind));

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
  struct prop_args *p_args = apr_palloc(fb->eb->edit_pool, sizeof(*p_args));

  p_args->name = apr_pstrdup(fb->eb->edit_pool, name);
  p_args->value = value ? svn_string_dup(value, fb->eb->edit_pool) : NULL;

  SVN_ERR(add_action(fb->eb, fb->path, ACTION_SET_PROP, p_args));

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
                      svn_delta_fetch_props_func_t fetch_props_func,
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





typedef enum action_code_t {
  ACTION_MV,
  ACTION_MKDIR,
  ACTION_CP,
  ACTION_PROPSET,
  ACTION_PUT,
  ACTION_RM
} action_code_t;

struct operation {
  enum {
    OP_OPEN,
    OP_DELETE,
    OP_ADD,
    OP_REPLACE,
    OP_PROPSET           /* only for files for which no other operation is
                            occuring; directories are OP_OPEN with non-empty
                            props */
  } operation;
  svn_kind_t kind;  /* to copy, mkdir, put or set revprops */
  svn_revnum_t rev;      /* to copy, valid for add and replace */
  const char *url;       /* to copy, valid for add and replace */
  const char *src_file;  /* for put, the source file for contents */
  apr_hash_t *children;  /* const char *path -> struct operation * */
  apr_hash_t *props;     /* const char *prop_name ->
                            const svn_string_t *prop_value */
  void *baton;           /* as returned by the commit editor */
};

struct editor_baton
{
  const svn_delta_editor_t *deditor;
  void *dedit_baton;

  svn_delta_fetch_kind_func_t fetch_kind_func;
  void *fetch_kind_baton;

  struct operation root;

  apr_hash_t *paths;
  apr_pool_t *edit_pool;
};

/* Find the operation associated with PATH, which is a single-path
   component representing a child of the path represented by
   OPERATION.  If no such child operation exists, create a new one of
   type OP_OPEN. */
static struct operation *
get_operation(const char *path,
              struct operation *operation,
              apr_pool_t *result_pool)
{
  struct operation *child = apr_hash_get(operation->children, path,
                                         APR_HASH_KEY_STRING);
  if (! child)
    {
      child = apr_pcalloc(result_pool, sizeof(*child));
      child->children = apr_hash_make(result_pool);
      child->operation = OP_OPEN;
      child->rev = SVN_INVALID_REVNUM;
      child->kind = svn_node_dir;
      child->props = NULL;
      apr_hash_set(operation->children, apr_pstrdup(result_pool, path),
                   APR_HASH_KEY_STRING, child);
    }
  return child;
}

/* Add PATH to the operations tree rooted at OPERATION, creating any
   intermediate nodes that are required.  Here's what's expected for
   each action type:

      ACTION          URL    REV      SRC-FILE  PROPNAME
      ------------    -----  -------  --------  --------
      ACTION_MKDIR    NULL   invalid  NULL      NULL
      ACTION_CP       valid  valid    NULL      NULL
      ACTION_PUT      NULL   invalid  valid     NULL
      ACTION_RM       NULL   invalid  NULL      NULL
      ACTION_PROPSET  valid  invalid  NULL      valid

   Node type information is obtained for any copy source (to determine
   whether to create a file or directory) and for any deleted path (to
   ensure it exists since svn_delta_editor_t->delete_entry doesn't
   return an error on non-existent nodes). */
static svn_error_t *
build(struct editor_baton *eb,
      action_code_t action,
      const char *relpath,
      const char *url,
      svn_revnum_t rev,
      apr_hash_t *props,
      const char *src_file,
      svn_revnum_t head,
      apr_pool_t *scratch_pool)
{
  apr_array_header_t *path_bits = svn_path_decompose(relpath, scratch_pool);
  const char *path_so_far = "";
  struct operation *operation = &eb->root;
  int i;

  /* We should only see PROPS when action is ACTION_PROPSET. */
  SVN_ERR_ASSERT((props && action == ACTION_PROPSET)
                || (!props && action != ACTION_PROPSET) );

  /* Look for any previous operations we've recognized for PATH.  If
     any of PATH's ancestors have not yet been traversed, we'll be
     creating OP_OPEN operations for them as we walk down PATH's path
     components. */
  for (i = 0; i < path_bits->nelts; ++i)
    {
      const char *path_bit = APR_ARRAY_IDX(path_bits, i, const char *);
      path_so_far = svn_relpath_join(path_so_far, path_bit, scratch_pool);
      operation = get_operation(path_so_far, operation, eb->edit_pool);
    }

  /* Handle property changes. */
  if (props)
    {
      SVN_ERR(eb->fetch_kind_func(&operation->kind, eb->fetch_kind_baton,
                                  relpath, scratch_pool));

      /* If we're not adding this thing ourselves, check for existence.  */
      if (! ((operation->operation == OP_ADD) ||
             (operation->operation == OP_REPLACE)))
        {
          if ((operation->kind == svn_node_file)
                   && (operation->operation == OP_OPEN))
            operation->operation = OP_PROPSET;
        }
      operation->props = svn_prop_hash_dup(props, eb->edit_pool);
      if (!operation->rev)
        operation->rev = rev;
      return SVN_NO_ERROR;
    }

  if (action == ACTION_RM)
    operation->operation = OP_DELETE;

  /* Handle copy operations (which can be adds or replacements). */
  else if (action == ACTION_CP)
    {
      operation->operation =
        operation->operation == OP_DELETE ? OP_REPLACE : OP_ADD;

      SVN_ERR(eb->fetch_kind_func(&operation->kind, eb->fetch_kind_baton,
                                  relpath, scratch_pool));
      operation->url = url;
      operation->rev = rev;
    }
  /* Handle mkdir operations (which can be adds or replacements). */
  else if (action == ACTION_MKDIR)
    {
      operation->operation =
        operation->operation == OP_DELETE ? OP_REPLACE : OP_ADD;
      operation->kind = svn_node_dir;
    }
  /* Handle put operations (which can be adds, replacements, or opens). */
  else if (action == ACTION_PUT)
    {
      if (operation->operation == OP_DELETE)
        {
          operation->operation = OP_REPLACE;
        }
      else
        {
          SVN_ERR(eb->fetch_kind_func(&operation->kind, eb->fetch_kind_baton,
                                      relpath, scratch_pool));
          if (operation->kind == svn_node_file)
            operation->operation = OP_OPEN;
          else if (operation->kind == svn_node_none)
            operation->operation = OP_ADD;
          else
            return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                                     "'%s' is not a file", relpath);
        }
      operation->kind = svn_node_file;
      operation->src_file = src_file;
    }
  else
    {
      /* We shouldn't get here. */
      SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_add_directory_t */
static svn_error_t *
add_directory_cb(void *baton,
                 const char *relpath,
                 const apr_array_header_t *children,
                 apr_hash_t *props,
                 svn_revnum_t replaces_rev,
                 apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;

  SVN_ERR(build(eb, ACTION_MKDIR, relpath, NULL, SVN_INVALID_REVNUM,
                NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));

  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_add_file_t */
static svn_error_t *
add_file_cb(void *baton,
            const char *relpath,
            const svn_checksum_t *checksum,
            svn_stream_t *contents,
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
              svn_kind_t kind,
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
  struct editor_baton *eb = baton;

  SVN_ERR(build(eb, ACTION_PROPSET, relpath, NULL, SVN_INVALID_REVNUM,
                props, NULL, SVN_INVALID_REVNUM, scratch_pool));

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

static svn_error_t *
drive(const struct operation *operation,
      const svn_delta_editor_t *editor,
      apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, operation->children);
       hi; hi = apr_hash_next(hi))
    {
      const struct operation *child;

      svn_pool_clear(iterpool);
      child = svn__apr_hash_index_val(hi);

      if (operation->kind == svn_node_dir)
        SVN_ERR(drive(child, editor, iterpool));
    }

  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_complete_t */
static svn_error_t *
complete_cb(void *baton,
            apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;

  /* Drive the tree we've created. */
  SVN_ERR(drive(&eb->root, eb->deditor, scratch_pool));

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
                      svn_delta_fetch_kind_func_t fetch_kind_func,
                      void *fetch_kind_baton,
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
  eb->edit_pool = result_pool;
  eb->paths = apr_hash_make(result_pool);

  eb->fetch_kind_func = fetch_kind_func;
  eb->fetch_kind_baton = fetch_kind_baton;

  eb->root.children = apr_hash_make(result_pool);
  eb->root.kind = svn_node_dir;
  eb->root.operation = OP_OPEN;
  eb->root.props = NULL;
  eb->root.rev = SVN_INVALID_REVNUM;

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
                         svn_delta_fetch_props_func_t fetch_props_func,
                         void *fetch_props_baton,
                         svn_delta_fetch_kind_func_t fetch_kind_func,
                         void *fetch_kind_baton,
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
                                NULL, NULL, fetch_kind_func, fetch_kind_baton,
                                result_pool, scratch_pool));
  SVN_ERR(svn_delta_from_editor(deditor_out, dedit_baton_out, editor,
                                fetch_props_func, fetch_props_baton,
                                result_pool));

#endif
  return SVN_NO_ERROR;
}
