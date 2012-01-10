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
#include "svn_hash.h"
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

typedef svn_error_t *(*start_edit_func_t)(
    void *baton,
    svn_revnum_t base_revision);

typedef svn_error_t *(*target_revision_func_t)(
    void *baton,
    svn_revnum_t target_revision,
    apr_pool_t *scratch_pool);

/* svn_editor__See insert_shims() for more information. */
struct extra_baton
{
  start_edit_func_t start_edit;
  target_revision_func_t target_revision;
  void *baton;
};

struct ev2_edit_baton
{
  svn_editor_t *editor;
  apr_hash_t *paths;
  apr_pool_t *edit_pool;
  struct extra_baton *exb;

  svn_boolean_t *found_abs_paths; /* Did we strip an incoming '/' from the
                                     paths?  */

  svn_delta_fetch_props_func_t fetch_props_func;
  void *fetch_props_baton;

  svn_delta_fetch_base_func_t fetch_base_func;
  void *fetch_base_baton;
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
  const char *delta_base;
};

enum action_code_t
{
  ACTION_MOVE,
  ACTION_MKDIR,
  ACTION_COPY,
  ACTION_PROPSET,
  ACTION_PUT,
  ACTION_ADD,
  ACTION_DELETE,
  ACTION_ADD_ABSENT,
  ACTION_SET_TEXT
};

struct path_action
{
  enum action_code_t action;
  void *args;
};

struct prop_args
{
  const char *name;
  const svn_string_t *value;
};

struct copy_args
{
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;
};

struct path_checksum_args
{
  const char *path;
  svn_checksum_t *checksum;
};

static svn_error_t *
add_action(struct ev2_edit_baton *eb,
           const char *path,
           enum action_code_t action,
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
  svn_boolean_t need_add = FALSE;
  svn_boolean_t need_delete = FALSE;
  apr_array_header_t *children;
  svn_stream_t *contents = NULL;
  svn_checksum_t *checksum = NULL;
  svn_revnum_t delete_revnum = SVN_INVALID_REVNUM;
  svn_kind_t kind;
  int i;

  if (*path == '/')
    {
      path++;
      *eb->found_abs_paths = TRUE;
    }

  /* Go through all of our actions, populating various datastructures
   * dependent on them. */
  for (i = 0; i < actions->nelts; i++)
    {
      const struct path_action *action = APR_ARRAY_IDX(actions, i,
                                                       struct path_action *);

      switch (action->action)
        {
          case ACTION_PROPSET:
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
              delete_revnum = *((svn_revnum_t *) action->args);
              need_delete = TRUE;
              break;
            }

          case ACTION_ADD:
            {
              kind = *((svn_kind_t *) action->args);
              need_add = TRUE;

              if (kind == svn_kind_dir)
                {
                  children = apr_array_make(scratch_pool, 1,
                                            sizeof(const char *));
                }
              else
                {
                  /* The default is an empty file. */
                  contents = svn_stream_empty(scratch_pool);
                  checksum = svn_checksum_empty_checksum(svn_checksum_sha1,
                                                         scratch_pool);
                }
              break;
            }

          case ACTION_SET_TEXT:
            {
              struct path_checksum_args *pca = action->args;

              SVN_ERR(svn_stream_open_readonly(&contents, pca->path,
                                               scratch_pool, scratch_pool));
              checksum = pca->checksum;
              break;
            }

          case ACTION_COPY:
            {
              struct copy_args *c_args = action->args;

              SVN_ERR(svn_editor_copy(eb->editor, c_args->copyfrom_path,
                                      c_args->copyfrom_rev, path,
                                      SVN_INVALID_REVNUM));
              break;
            }

          case ACTION_ADD_ABSENT:
            {
              kind = *((svn_kind_t *) action->args);
              SVN_ERR(svn_editor_add_absent(eb->editor, path, kind,
                                            SVN_INVALID_REVNUM));
              break;
            }

          default:
            SVN_ERR_MALFUNCTION();
        }
    }

  /* We've now got a wholistic view of what has happened to this node,
   * so we can call our own editor APIs on it. */

  if (need_delete && !need_add)
    {
      /* If we're only doing a delete, do it here. */
      SVN_ERR(svn_editor_delete(eb->editor, path, delete_revnum));
    }

  if (need_add)
    {
      if (kind == svn_kind_dir)
        {
          SVN_ERR(svn_editor_add_directory(eb->editor, path, children,
                                           props, delete_revnum));
        }
      else
        {
          SVN_ERR(svn_editor_add_file(eb->editor, path, checksum, contents,
                                      props, delete_revnum));
        }
    }
  else
    {
      if (props)
        {
          /* We fetched and modified the props in some way. Apply 'em now that
             we have the new set.  */
          SVN_ERR(svn_editor_set_props(eb->editor, path, SVN_INVALID_REVNUM,
                                       props, contents == NULL));
        }

      if (contents)
        {
          /* If we have an content for this node, set it now. */
          SVN_ERR(svn_editor_set_text(eb->editor, path, SVN_INVALID_REVNUM,
                                      checksum, contents));
        }
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
run_ev2_actions(void *edit_baton,
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

      /* Remove this item from the hash. */
      apr_hash_set(eb->paths, path, APR_HASH_KEY_STRING, NULL);
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_set_target_revision(void *edit_baton,
                        svn_revnum_t target_revision,
                        apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;

  if (eb->exb->target_revision)
    SVN_ERR(eb->exb->target_revision(eb->exb->baton, target_revision,
                                     scratch_pool));

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

  if (eb->exb->start_edit)
    SVN_ERR(eb->exb->start_edit(eb->exb->baton, base_revision));

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

  cb->eb = pb->eb;
  cb->path = apr_pstrdup(result_pool, path);
  *child_baton = cb;

  if (!copyfrom_path)
    {
      /* A simple add. */
      svn_kind_t *kind = apr_palloc(pb->eb->edit_pool, sizeof(*kind));

      *kind = svn_kind_dir;
      SVN_ERR(add_action(pb->eb, path, ACTION_ADD, kind));
    }
  else
    {
      /* A copy */
      struct copy_args *args = apr_palloc(pb->eb->edit_pool, sizeof(*args));

      args->copyfrom_path = apr_pstrdup(pb->eb->edit_pool, copyfrom_path);
      args->copyfrom_rev = copyfrom_revision;
      SVN_ERR(add_action(pb->eb, path, ACTION_COPY, args));
    }

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

  SVN_ERR(add_action(db->eb, db->path, ACTION_PROPSET, p_args));

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_close_directory(void *dir_baton,
                    apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_absent_directory(const char *path,
                     void *parent_baton,
                     apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *pb = parent_baton;
  svn_kind_t *kind = apr_palloc(pb->eb->edit_pool, sizeof(*kind));
  
  *kind = svn_kind_dir;
  SVN_ERR(add_action(pb->eb, path, ACTION_ADD_ABSENT, kind));

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
  fb->path = apr_pstrdup(result_pool, path);
  *file_baton = fb;

  SVN_ERR(fb->eb->fetch_base_func(&fb->delta_base,
                                  fb->eb->fetch_base_baton,
                                  path, result_pool, result_pool));

  if (!copyfrom_path)
    {
      /* A simple add. */
      svn_kind_t *kind = apr_palloc(pb->eb->edit_pool, sizeof(*kind));

      *kind = svn_kind_file;
      SVN_ERR(add_action(pb->eb, path, ACTION_ADD, kind));
    }
  else
    {
      /* A copy */
      struct copy_args *args = apr_palloc(pb->eb->edit_pool, sizeof(*args));

      args->copyfrom_path = apr_pstrdup(pb->eb->edit_pool, copyfrom_path);
      args->copyfrom_rev = copyfrom_revision;
      SVN_ERR(add_action(pb->eb, path, ACTION_COPY, args));
    }

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

  SVN_ERR(fb->eb->fetch_base_func(&fb->delta_base,
                                  fb->eb->fetch_base_baton,
                                  path, result_pool, result_pool));

  *file_baton = fb;
  return SVN_NO_ERROR;
}

struct handler_baton
{
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  apr_pool_t *pool;
};

static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  struct handler_baton *hb = baton;
  svn_error_t *err;

  err = hb->apply_handler(window, hb->apply_baton);
  if (window != NULL && !err)
    return SVN_NO_ERROR;

  svn_pool_destroy(hb->pool);

  return svn_error_trace(err);
}


static svn_error_t *
ev2_apply_textdelta(void *file_baton,
                    const char *base_checksum,
                    apr_pool_t *result_pool,
                    svn_txdelta_window_handler_t *handler,
                    void **handler_baton)
{
  struct ev2_file_baton *fb = file_baton;
  apr_pool_t *handler_pool = svn_pool_create(fb->eb->edit_pool);
  struct handler_baton *hb = apr_pcalloc(handler_pool, sizeof(*hb));
  svn_stream_t *source;
  svn_stream_t *target;
  struct path_checksum_args *pca = apr_pcalloc(fb->eb->edit_pool,
                                               sizeof(*pca));

  if (! fb->delta_base)
    source = svn_stream_empty(handler_pool);
  else
    SVN_ERR(svn_stream_open_readonly(&source, fb->delta_base, handler_pool,
                                     result_pool));

  SVN_ERR(svn_stream_open_unique(&target, &pca->path, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 fb->eb->edit_pool, result_pool));

  /* Wrap our target with a checksum'ing stream. */
  target = svn_stream_checksummed2(target, NULL, &pca->checksum,
                                   svn_checksum_sha1, TRUE,
                                   fb->eb->edit_pool);

  svn_txdelta_apply(source, target,
                    NULL, NULL,
                    handler_pool,
                    &hb->apply_handler, &hb->apply_baton);

  hb->pool = handler_pool;
                    
  *handler_baton = hb;
  *handler = window_handler;

  SVN_ERR(add_action(fb->eb, fb->path, ACTION_SET_TEXT, pca));

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

  SVN_ERR(add_action(fb->eb, fb->path, ACTION_PROPSET, p_args));

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_close_file(void *file_baton,
               const char *text_checksum,
               apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_absent_file(const char *path,
                void *parent_baton,
                apr_pool_t *scratch_pool)
{
  struct ev2_dir_baton *pb = parent_baton;
  svn_kind_t *kind = apr_palloc(pb->eb->edit_pool, sizeof(*kind));
  
  *kind = svn_kind_file;
  SVN_ERR(add_action(pb->eb, path, ACTION_ADD_ABSENT, kind));

  return SVN_NO_ERROR;
}

static svn_error_t *
ev2_close_edit(void *edit_baton,
               apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;

  SVN_ERR(run_ev2_actions(edit_baton, scratch_pool));
  return svn_error_trace(svn_editor_complete(eb->editor));
}

static svn_error_t *
ev2_abort_edit(void *edit_baton,
               apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;

  SVN_ERR(run_ev2_actions(edit_baton, scratch_pool));
  return svn_error_trace(svn_editor_abort(eb->editor));
}

static svn_error_t *
delta_from_editor(const svn_delta_editor_t **deditor,
                  void **dedit_baton,
                  svn_editor_t *editor,
                  svn_boolean_t *found_abs_paths,
                  svn_delta_fetch_props_func_t fetch_props_func,
                  void *fetch_props_baton,
                  svn_delta_fetch_base_func_t fetch_base_func,
                  void *fetch_base_baton,
                  struct extra_baton *exb,
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
  struct ev2_edit_baton *eb = apr_pcalloc(pool, sizeof(*eb));

  eb->editor = editor;
  eb->paths = apr_hash_make(pool);
  eb->edit_pool = pool;
  eb->found_abs_paths = found_abs_paths;
  *eb->found_abs_paths = FALSE;
  eb->exb = exb;

  eb->fetch_props_func = fetch_props_func;
  eb->fetch_props_baton = fetch_props_baton;

  eb->fetch_base_func = fetch_base_func;
  eb->fetch_base_baton = fetch_base_baton;

  *dedit_baton = eb;
  *deditor = &delta_editor;

  return SVN_NO_ERROR;
}





struct operation {
  enum {
    OP_OPEN,
    OP_DELETE,
    OP_ADD,
    OP_REPLACE,
    OP_ADD_ABSENT,
    OP_PROPSET           /* only for files for which no other operation is
                            occuring; directories are OP_OPEN with non-empty
                            props */
  } operation;
  svn_kind_t kind;  /* to copy, mkdir, put or set revprops */
  svn_revnum_t copyfrom_revision;      /* to copy, valid for add and replace */
  const char *copyfrom_url;       /* to copy, valid for add and replace */
  const char *src_file;  /* for put, the source file for contents */
  apr_hash_t *children;  /* const char *path -> struct operation * */
  apr_hash_t *prop_mods; /* const char *prop_name ->
                            const svn_string_t *prop_value */
  apr_array_header_t *prop_dels; /* const char *prop_name deletions */
  void *baton;           /* as returned by the commit editor */
};

struct editor_baton
{
  const svn_delta_editor_t *deditor;
  void *dedit_baton;

  svn_delta_fetch_kind_func_t fetch_kind_func;
  void *fetch_kind_baton;

  svn_delta_fetch_props_func_t fetch_props_func;
  void *fetch_props_baton;

  struct operation root;
  svn_boolean_t root_opened;
  svn_boolean_t *make_abs_paths;

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
      child->copyfrom_revision = SVN_INVALID_REVNUM;
      child->kind = svn_kind_dir;
      child->prop_mods = apr_hash_make(result_pool);
      child->prop_dels = apr_array_make(result_pool, 1, sizeof(const char *));
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
      ACTION_COPY     valid  valid    NULL      NULL
      ACTION_PUT      NULL   invalid  valid     NULL
      ACTION_DELETE   NULL   invalid  NULL      NULL
      ACTION_PROPSET  valid  invalid  NULL      valid

   Node type information is obtained for any copy source (to determine
   whether to create a file or directory) and for any deleted path (to
   ensure it exists since svn_delta_editor_t->delete_entry doesn't
   return an error on non-existent nodes). */
static svn_error_t *
build(struct editor_baton *eb,
      enum action_code_t action,
      const char *relpath,
      svn_kind_t kind,
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
      apr_hash_t *current_props;
      apr_array_header_t *propdiffs;

      if (kind == svn_kind_unknown)
        SVN_ERR(eb->fetch_kind_func(&operation->kind, eb->fetch_kind_baton,
                                    relpath, scratch_pool));
      else
        operation->kind = kind;

      SVN_ERR(eb->fetch_props_func(&current_props, eb->fetch_props_baton,
                                   relpath, scratch_pool, scratch_pool));

      /* Use the edit pool, since most of the results will need to be
         persisted. */
      SVN_ERR(svn_prop_diffs(&propdiffs, props, current_props, eb->edit_pool));

      for (i = 0; i < propdiffs->nelts; i++)
        {
          /* Note: the array returned by svn_prop_diffs() is an array of
             actual structures, not pointers to them. */
          svn_prop_t *prop = &APR_ARRAY_IDX(propdiffs, i, svn_prop_t);
          if (!prop->value)
            APR_ARRAY_PUSH(operation->prop_dels, const char *) = prop->name;
          else
            apr_hash_set(operation->prop_mods, prop->name, APR_HASH_KEY_STRING,
                         prop->value);
        }

      /* If we're not adding this thing ourselves, check for existence.  */
      if (! ((operation->operation == OP_ADD) ||
             (operation->operation == OP_REPLACE)))
        {
          if ((operation->kind == svn_kind_file)
                   && (operation->operation == OP_OPEN))
            operation->operation = OP_PROPSET;
        }
      if (!operation->copyfrom_revision)
        operation->copyfrom_revision = rev;
      return SVN_NO_ERROR;
    }

  if (action == ACTION_DELETE)
    operation->operation = OP_DELETE;

  else if (action == ACTION_ADD_ABSENT)
    operation->operation = OP_ADD_ABSENT;

  /* Handle copy operations (which can be adds or replacements). */
  else if (action == ACTION_COPY)
    {
      operation->operation =
        operation->operation == OP_DELETE ? OP_REPLACE : OP_ADD;

      if (kind == svn_kind_none)
        SVN_ERR(eb->fetch_kind_func(&operation->kind, eb->fetch_kind_baton,
                                    relpath, scratch_pool));
      else
        operation->kind = kind;
      operation->copyfrom_url = url;
      operation->copyfrom_revision = rev;
    }
  /* Handle mkdir operations (which can be adds or replacements). */
  else if (action == ACTION_MKDIR)
    {
      operation->operation =
        operation->operation == OP_DELETE ? OP_REPLACE : OP_ADD;
      operation->kind = svn_kind_dir;
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
          if (kind == svn_kind_file)
            operation->operation = OP_OPEN;
          else if (kind == svn_kind_none)
            operation->operation = OP_ADD;
          else
            return svn_error_createf(SVN_ERR_BAD_URL, NULL,
                                     "'%s' is not a file", relpath);
        }
      operation->kind = svn_kind_file;
      operation->src_file = src_file;
    }
  else
    {
      /* We shouldn't get here. */
      SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
ensure_root_opened(struct editor_baton *eb)
{
  if (!eb->root_opened)
    {
      SVN_ERR(eb->deditor->open_root(eb->dedit_baton, SVN_INVALID_REVNUM,
                                     eb->edit_pool, &eb->root.baton));
      eb->root_opened = TRUE;
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

  SVN_ERR(ensure_root_opened(eb));

  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      /* We need to add the delete action. */

      SVN_ERR(build(eb, ACTION_DELETE, relpath, svn_kind_unknown,
                    NULL, SVN_INVALID_REVNUM,
                    NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));
    }

  SVN_ERR(build(eb, ACTION_MKDIR, relpath, svn_kind_dir,
                NULL, SVN_INVALID_REVNUM,
                NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));

  if (props && apr_hash_count(props) > 0)
    SVN_ERR(build(eb, ACTION_PROPSET, relpath, svn_kind_dir,
                  NULL, SVN_INVALID_REVNUM, props,
                  NULL, SVN_INVALID_REVNUM, scratch_pool));

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
  struct editor_baton *eb = baton;
  const char *tmp_filename;
  svn_stream_t *tmp_stream;

  SVN_ERR(ensure_root_opened(eb));

  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      /* We need to add the delete action. */

      SVN_ERR(build(eb, ACTION_DELETE, relpath, svn_kind_unknown,
                    NULL, SVN_INVALID_REVNUM,
                    NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));
    }

  /* Spool the contents to a tempfile, and provide that to the driver. */
  SVN_ERR(svn_stream_open_unique(&tmp_stream, &tmp_filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 eb->edit_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(svn_stream_disown(contents, scratch_pool),
                           tmp_stream, NULL, NULL, scratch_pool));

  SVN_ERR(build(eb, ACTION_PUT, relpath, svn_kind_none,
                NULL, SVN_INVALID_REVNUM,
                NULL, tmp_filename, SVN_INVALID_REVNUM, scratch_pool));

  if (props && apr_hash_count(props) > 0)
    SVN_ERR(build(eb, ACTION_PROPSET, relpath, svn_kind_file,
                  NULL, SVN_INVALID_REVNUM, props,
                  NULL, SVN_INVALID_REVNUM, scratch_pool));

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
  struct editor_baton *eb = baton;

  SVN_ERR(ensure_root_opened(eb));

  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      /* We need to add the delete action. */

      SVN_ERR(build(eb, ACTION_DELETE, relpath, svn_kind_unknown,
                    NULL, SVN_INVALID_REVNUM,
                    NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));
    }

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
  struct editor_baton *eb = baton;

  SVN_ERR(ensure_root_opened(eb));

  SVN_ERR(build(eb, ACTION_ADD_ABSENT, relpath, kind,
                NULL, SVN_INVALID_REVNUM,
                NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));

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

  SVN_ERR(ensure_root_opened(eb));

  SVN_ERR(build(eb, ACTION_PROPSET, relpath, svn_kind_unknown,
                NULL, SVN_INVALID_REVNUM,
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
  struct editor_baton *eb = baton;
  const char *tmp_filename;
  svn_stream_t *tmp_stream;

  SVN_ERR(ensure_root_opened(eb));

  /* Spool the contents to a tempfile, and provide that to the driver. */
  SVN_ERR(svn_stream_open_unique(&tmp_stream, &tmp_filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 eb->edit_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(svn_stream_disown(contents, scratch_pool),
                           tmp_stream, NULL, NULL, scratch_pool));

  SVN_ERR(build(eb, ACTION_PUT, relpath, svn_kind_file,
                NULL, SVN_INVALID_REVNUM,
                NULL, tmp_filename, SVN_INVALID_REVNUM, scratch_pool));

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
  struct editor_baton *eb = baton;

  SVN_ERR(ensure_root_opened(eb));

  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_delete_t */
static svn_error_t *
delete_cb(void *baton,
          const char *relpath,
          svn_revnum_t revision,
          apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;

  SVN_ERR(ensure_root_opened(eb));

  SVN_ERR(build(eb, ACTION_DELETE, relpath, svn_kind_unknown,
                NULL, SVN_INVALID_REVNUM, NULL, NULL, SVN_INVALID_REVNUM,
                scratch_pool));

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
  struct editor_baton *eb = baton;

  SVN_ERR(ensure_root_opened(eb));

  SVN_ERR(build(eb, ACTION_COPY, dst_relpath, svn_kind_unknown,
                src_relpath, src_revision, NULL, NULL, SVN_INVALID_REVNUM,
                scratch_pool));

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
  struct editor_baton *eb = baton;

  SVN_ERR(ensure_root_opened(eb));

  return SVN_NO_ERROR;
}

static svn_error_t *
change_props(const svn_delta_editor_t *editor,
             void *baton,
             const struct operation *child,
             apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  if (child->prop_dels)
    {
      int i;
      for (i = 0; i < child->prop_dels->nelts; i++)
        {
          const char *prop_name;

          svn_pool_clear(iterpool);
          prop_name = APR_ARRAY_IDX(child->prop_dels, i, const char *);
          if (child->kind == svn_kind_dir)
            SVN_ERR(editor->change_dir_prop(baton, prop_name,
                                            NULL, iterpool));
          else
            SVN_ERR(editor->change_file_prop(baton, prop_name,
                                             NULL, iterpool));
        }
    }

  if (apr_hash_count(child->prop_mods))
    {
      apr_hash_index_t *hi;
      for (hi = apr_hash_first(scratch_pool, child->prop_mods);
           hi; hi = apr_hash_next(hi))
        {
          const char *name = svn__apr_hash_index_key(hi);
          svn_string_t *val = svn__apr_hash_index_val(hi);

          svn_pool_clear(iterpool);
          if (child->kind == svn_kind_dir)
            SVN_ERR(editor->change_dir_prop(baton, name, val, iterpool));
          else
            SVN_ERR(editor->change_file_prop(baton, name, val, iterpool));
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
drive_tree(const struct operation *operation,
           const svn_delta_editor_t *editor,
           svn_boolean_t *make_abs_paths,
           apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, operation->children);
       hi; hi = apr_hash_next(hi))
    {
      struct operation *child;
      const char *path;
      void *file_baton = NULL;

      svn_pool_clear(iterpool);
      child = svn__apr_hash_index_val(hi);
      path = svn__apr_hash_index_key(hi);

      if (path[0] != '/' && *make_abs_paths)
        path = apr_pstrcat(iterpool, "/", path, NULL);

      /* Deletes and replacements are simple -- just delete the thing. */
      if (child->operation == OP_DELETE || child->operation == OP_REPLACE)
        {
          SVN_ERR(editor->delete_entry(path, SVN_INVALID_REVNUM,
                                       operation->baton, iterpool));
        }

      if (child->operation == OP_OPEN || child->operation == OP_PROPSET)
        {
          if (child->kind == svn_kind_dir)
            SVN_ERR(editor->open_directory(path, operation->baton,
                                           SVN_INVALID_REVNUM,
                                           iterpool, &child->baton));
          else
            SVN_ERR(editor->open_file(path, operation->baton,
                                      SVN_INVALID_REVNUM,
                                      iterpool, &file_baton));
        }

      if (child->operation == OP_ADD || child->operation == OP_REPLACE)
        {
          if (child->kind == svn_kind_dir)
            SVN_ERR(editor->add_directory(path, operation->baton,
                                          child->copyfrom_url,
                                          child->copyfrom_revision,
                                          iterpool, &child->baton));
          else
            SVN_ERR(editor->add_file(path, operation->baton,
                                     child->copyfrom_url,
                                     child->copyfrom_revision, iterpool,
                                     &file_baton));
        }

      if (child->operation == OP_ADD_ABSENT)
        {
          if (child->kind == svn_kind_dir)
            SVN_ERR(editor->absent_directory(path, operation->baton,
                                             iterpool));
          else
            SVN_ERR(editor->absent_file(path, operation->baton, iterpool));
        }

      if (child->src_file && file_baton)
        {
          /* We need to change textual contents. */
          svn_txdelta_window_handler_t handler;
          void *handler_baton;
          svn_stream_t *contents;

          SVN_ERR(editor->apply_textdelta(file_baton, NULL, iterpool,
                                          &handler, &handler_baton));
          SVN_ERR(svn_stream_open_readonly(&contents, child->src_file,
                                           iterpool, iterpool));
          SVN_ERR(svn_txdelta_send_stream(contents, handler, handler_baton,
                                          NULL, iterpool));
          SVN_ERR(svn_stream_close(contents));
        }

      /* Only worry about properties and closing the file baton if we've
         previously opened it. */
      if (file_baton)
        {
          if (child->kind == svn_kind_file)
            SVN_ERR(change_props(editor, file_baton, child, iterpool));
          SVN_ERR(editor->close_file(file_baton, NULL, iterpool));
        }

      /* We *always* open the child directory, so drive the child, change any
         props, and then close the directory. */
      if (child->kind == svn_kind_dir
                   && (child->operation == OP_OPEN
                    || child->operation == OP_PROPSET
                    || child->operation == OP_ADD
                    || child->operation == OP_REPLACE))
        {
          SVN_ERR(drive_tree(child, editor, make_abs_paths, iterpool));
          SVN_ERR(editor->close_directory(child->baton, iterpool));
        }
    }
  svn_pool_destroy(iterpool);

  /* Finally, for this node, if it's a directory, change any props before
     returning (our caller will close the directory. */
  if (operation->kind == svn_kind_dir
                   && (operation->operation == OP_OPEN
                    || operation->operation == OP_PROPSET
                    || operation->operation == OP_ADD))
    {
      SVN_ERR(change_props(editor, operation->baton, operation, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_complete_t */
static svn_error_t *
complete_cb(void *baton,
            apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;
  svn_error_t *err;

  SVN_ERR(ensure_root_opened(eb));

  /* Drive the tree we've created. */
  err = drive_tree(&eb->root, eb->deditor, eb->make_abs_paths, scratch_pool);
  if (!err)
     {
       err = eb->deditor->close_directory(eb->root.baton, scratch_pool);
       err = svn_error_compose_create(err, eb->deditor->close_edit(
                                                            eb->dedit_baton,
                                                            scratch_pool));
     }

  if (err)
    svn_error_clear(eb->deditor->abort_edit(eb->dedit_baton, scratch_pool));

  return svn_error_trace(err);
}

/* This implements svn_editor_cb_abort_t */
static svn_error_t *
abort_cb(void *baton,
         apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;
  svn_error_t *err;
  svn_error_t *err2;

  /* We still need to drive anything we collected in the editor to this
     point. */

  /* Drive the tree we've created. */
  err = drive_tree(&eb->root, eb->deditor, eb->make_abs_paths, scratch_pool);

  err2 = eb->deditor->abort_edit(eb->dedit_baton, scratch_pool);

  if (err2)
    {
      if (err)
        svn_error_clear(err2);
      else
        err = err2;
    }

  return svn_error_trace(err);
}

static svn_error_t *
start_edit_func(void *baton,
                svn_revnum_t base_revision)
{
  struct editor_baton *eb = baton;

  SVN_ERR(ensure_root_opened(eb));

  return SVN_NO_ERROR;
}

static svn_error_t *
target_revision_func(void *baton,
                     svn_revnum_t target_revision,
                     apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;

  SVN_ERR(eb->deditor->set_target_revision(eb->dedit_baton, target_revision,
                                           scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
editor_from_delta(svn_editor_t **editor_p,
                  struct extra_baton **exb,
                  const svn_delta_editor_t *deditor,
                  void *dedit_baton,
                  svn_boolean_t *send_abs_paths,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  svn_delta_fetch_kind_func_t fetch_kind_func,
                  void *fetch_kind_baton,
                  svn_delta_fetch_props_func_t fetch_props_func,
                  void *fetch_props_baton,
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
  struct extra_baton *extra_baton = apr_palloc(result_pool,
                                               sizeof(*extra_baton));

  eb->deditor = deditor;
  eb->dedit_baton = dedit_baton;
  eb->edit_pool = result_pool;
  eb->paths = apr_hash_make(result_pool);

  eb->fetch_kind_func = fetch_kind_func;
  eb->fetch_kind_baton = fetch_kind_baton;
  eb->fetch_props_func = fetch_props_func;
  eb->fetch_props_baton = fetch_props_baton;

  eb->root.children = apr_hash_make(result_pool);
  eb->root.kind = svn_kind_dir;
  eb->root.operation = OP_OPEN;
  eb->root.prop_mods = apr_hash_make(result_pool);
  eb->root.prop_dels = apr_array_make(result_pool, 1, sizeof(const char *));
  eb->root.copyfrom_revision = SVN_INVALID_REVNUM;

  eb->root_opened = FALSE;
  eb->make_abs_paths = send_abs_paths;

  SVN_ERR(svn_editor_create(&editor, eb, cancel_func, cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(editor, &editor_cbs, scratch_pool));

  *editor_p = editor;

  extra_baton->start_edit = start_edit_func;
  extra_baton->target_revision = target_revision_func;
  extra_baton->baton = eb;

  *exb = extra_baton;

  return SVN_NO_ERROR;
}

svn_delta_shim_callbacks_t *
svn_delta_shim_callbacks_default(apr_pool_t *result_pool)
{
  svn_delta_shim_callbacks_t *shim_callbacks = apr_pcalloc(result_pool,
                                                     sizeof(*shim_callbacks));
  return shim_callbacks;
}

/* Uncomment below to add editor shims throughout Subversion.  In it's
 * current state, that will likely break The World. */
/* #define ENABLE_EDITOR_SHIMS*/

svn_error_t *
svn_editor__insert_shims(const svn_delta_editor_t **deditor_out,
                         void **dedit_baton_out,
                         const svn_delta_editor_t *deditor_in,
                         void *dedit_baton_in,
                         svn_delta_shim_callbacks_t *shim_callbacks,
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

  /* The "extra baton" is a set of functions and a baton which allows the
     shims to communicate additional events to each other.
     editor_from_delta() returns a pointer to this baton, which
     delta_from_editor() should then store. */
  struct extra_baton *exb;

  /* The reason this is a pointer is that we don't know the appropriate
     value until we start receiving paths.  So process_actions() sets the
     flag, which drive_tree() later consumes. */
  svn_boolean_t *found_abs_paths = apr_palloc(result_pool,
                                              sizeof(*found_abs_paths));

  SVN_ERR(editor_from_delta(&editor, &exb, deditor_in, dedit_baton_in,
                            found_abs_paths, NULL, NULL,
                            shim_callbacks->fetch_kind_func,
                            shim_callbacks->fetch_baton,
                            shim_callbacks->fetch_props_func,
                            shim_callbacks->fetch_baton,
                            result_pool, scratch_pool));
  SVN_ERR(delta_from_editor(deditor_out, dedit_baton_out, editor,
                            found_abs_paths,
                            shim_callbacks->fetch_props_func,
                            shim_callbacks->fetch_baton,
                            shim_callbacks->fetch_base_func,
                            shim_callbacks->fetch_baton,
                            exb, result_pool));

#endif
  return SVN_NO_ERROR;
}
