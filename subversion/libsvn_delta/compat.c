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
  struct file_rev_handler_wrapper_baton *fwb = apr_pcalloc(pool, sizeof(*fwb));

  /* Set the user provided old format callback in the baton. */
  fwb->baton = handler_baton;
  fwb->handler = handler;

  *handler2_baton = fwb;
  *handler2 = file_rev_handler_wrapper;
}


/* The following code maps the calls to a traditional delta editor to an
 * Editorv2 editor.  It does this by keeping track of a lot of state, and
 * then communicating that state to Ev2 upon closure of the file or dir (or
 * edit).  Note that Ev2 calls add_symlink() and alter_symlink() are not
 * present in the delta editor paradigm, so we never call them.
 *
 * The general idea here is that we have to see *all* the actions on a node's
 * parent before we can process that node, which means we need to buffer a
 * large amount of information in the dir batons, and then process it in the
 * close_directory() handler.
 *
 * There are a few ways we alter the callback stream.  One is when unlocking
 * paths.  To tell a client a path should be unlocked, the server sends a
 * prop-del for the SVN_PROP_ENTRY_LOCK_TOKEN property.  This causes problems,
 * since the client doesn't have this property in the first place, but the
 * deletion has side effects (unlike deleting a non-existent regular property
 * would).  To solve this, we introduce *another* function into the API, not
 * a part of the Ev2 callbacks, but a companion which is used to register
 * the unlock of a path.  See ev2_change_file_prop() for implemenation
 * details.
 */

typedef svn_error_t *(*start_edit_func_t)(
    void *baton,
    svn_revnum_t base_revision);

typedef svn_error_t *(*target_revision_func_t)(
    void *baton,
    svn_revnum_t target_revision,
    apr_pool_t *scratch_pool);

typedef svn_error_t *(*unlock_func_t)(
    void *baton,
    const char *path,
    apr_pool_t *scratch_pool);

/* See svn_editor__insert_shims() for more information. */
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
  svn_boolean_t closed;

  svn_boolean_t *found_abs_paths; /* Did we strip an incoming '/' from the
                                     paths?  */

  svn_delta_fetch_props_func_t fetch_props_func;
  void *fetch_props_baton;

  svn_delta_fetch_base_func_t fetch_base_func;
  void *fetch_base_baton;

  unlock_func_t do_unlock;
  void *unlock_baton;
};

struct ev2_dir_baton
{
  struct ev2_edit_baton *eb;
  const char *path;
  svn_revnum_t base_revision;

  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;
};

struct ev2_file_baton
{
  struct ev2_edit_baton *eb;
  const char *path;
  svn_revnum_t base_revision;
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
  ACTION_SET_TEXT,
  ACTION_UNLOCK
};

struct path_action
{
  enum action_code_t action;
  void *args;
};

struct prop_args
{
  const char *name;
  svn_revnum_t base_revision;
  const svn_string_t *value;
  svn_kind_t kind;
};

struct copy_args
{
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;
};

struct path_checksum_args
{
  const char *path;
  svn_revnum_t base_revision;
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

/* Find all the paths which are immediate children of PATH and return their
   basenames in a list. */
static apr_array_header_t *
get_children(struct ev2_edit_baton *eb,
             const char *path,
             apr_pool_t *pool)
{
  apr_array_header_t *children = apr_array_make(pool, 1, sizeof(const char *));
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(pool, eb->paths); hi; hi = apr_hash_next(hi))
    {
      const char *p = svn__apr_hash_index_key(hi);
      const char *child;

      /* Sanitize our paths. */
      if (*p == '/')
        p++;
      
      /* Find potential children. */
      child = svn_relpath_skip_ancestor(path, p);
      if (!child || !*child)
        continue;

      /* If we have a path separator, it's a deep child, so just ignore it.
         ### Is there an API we should be using for this? */
      if (strchr(child, '/') != NULL)
        continue;

      APR_ARRAY_PUSH(children, const char *) = child;
    }

  return children;
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
  svn_boolean_t need_copy = FALSE;
  const char *copyfrom_path;
  svn_revnum_t copyfrom_rev;
  apr_array_header_t *children = NULL;
  svn_stream_t *contents = NULL;
  svn_checksum_t *checksum = NULL;
  svn_revnum_t delete_revnum = SVN_INVALID_REVNUM;
  svn_revnum_t props_base_revision = SVN_INVALID_REVNUM;
  svn_revnum_t text_base_revision = SVN_INVALID_REVNUM;
  svn_kind_t kind = svn_kind_unknown;
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

              kind = p_args->kind;

              if (!SVN_IS_VALID_REVNUM(props_base_revision))
                props_base_revision = p_args->base_revision;
              else
                SVN_ERR_ASSERT(p_args->base_revision == props_base_revision);

              if (!props)
                {
                  /* Fetch the original props. We can then apply each of
                     the modifications to it.  */
                  if (need_delete && need_add)
                    props = apr_hash_make(scratch_pool);
                  else if (need_copy)
                    SVN_ERR(eb->fetch_props_func(&props,
                                                 eb->fetch_props_baton,
                                                 copyfrom_path,
                                                 copyfrom_rev,
                                                 scratch_pool, scratch_pool));
                  else
                    SVN_ERR(eb->fetch_props_func(&props,
                                                 eb->fetch_props_baton,
                                                 path, props_base_revision,
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
                  children = get_children(eb, path, scratch_pool);
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

              /* We can only set text on files. */
              kind = svn_kind_file;

              SVN_ERR(svn_io_file_checksum2(&checksum, pca->path,
                                            svn_checksum_sha1, scratch_pool));
              SVN_ERR(svn_stream_open_readonly(&contents, pca->path,
                                               scratch_pool, scratch_pool));

              if (!SVN_IS_VALID_REVNUM(text_base_revision))
                text_base_revision = pca->base_revision;
              else
                SVN_ERR_ASSERT(pca->base_revision == text_base_revision);

              break;
            }

          case ACTION_COPY:
            {
              struct copy_args *c_args = action->args;

              copyfrom_path = c_args->copyfrom_path;
              copyfrom_rev = c_args->copyfrom_rev;
              need_copy = TRUE;
              break;
            }

          case ACTION_ADD_ABSENT:
            {
              kind = *((svn_kind_t *) action->args);
              SVN_ERR(svn_editor_add_absent(eb->editor, path, kind,
                                            SVN_INVALID_REVNUM));
              break;
            }

          case ACTION_UNLOCK:
            {
              SVN_ERR(eb->do_unlock(eb->unlock_baton, path, scratch_pool));
              break;
            }

          default:
            SVN_ERR_MALFUNCTION();
        }
    }

  /* We've now got a wholistic view of what has happened to this node,
   * so we can call our own editor APIs on it. */

  if (need_delete && !need_add && !need_copy)
    {
      /* If we're only doing a delete, do it here. */
      SVN_ERR(svn_editor_delete(eb->editor, path, delete_revnum));
      return SVN_NO_ERROR;
    }

  if (need_add)
    {
      if (props == NULL)
        props = apr_hash_make(scratch_pool);

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

      return SVN_NO_ERROR;
    }

  if (need_copy)
    {
      SVN_ERR(svn_editor_copy(eb->editor, copyfrom_path, copyfrom_rev, path,
                              delete_revnum));
    }

  if (props || contents)
    {
      /* We fetched and modified the props or content in some way. Apply 'em
         now.  */
      svn_revnum_t base_revision;

      if (SVN_IS_VALID_REVNUM(props_base_revision)
            && SVN_IS_VALID_REVNUM(text_base_revision))
        SVN_ERR_ASSERT(props_base_revision == text_base_revision);

      if (SVN_IS_VALID_REVNUM(props_base_revision))
        base_revision = props_base_revision;
      else if (SVN_IS_VALID_REVNUM(text_base_revision))
        base_revision = text_base_revision;
      else
        base_revision = SVN_INVALID_REVNUM;

      if (kind == svn_kind_dir)
        SVN_ERR(svn_editor_alter_directory(eb->editor, path, base_revision,
                                           props));
      else
        SVN_ERR(svn_editor_alter_file(eb->editor, path, base_revision, props,
                                      checksum, contents));
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
  struct ev2_dir_baton *db = apr_pcalloc(result_pool, sizeof(*db));
  struct ev2_edit_baton *eb = edit_baton;

  db->eb = eb;
  db->path = "";
  db->base_revision = base_revision;

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
  struct ev2_dir_baton *cb = apr_pcalloc(result_pool, sizeof(*cb));

  cb->eb = pb->eb;
  cb->path = apr_pstrdup(result_pool, path);
  cb->base_revision = pb->base_revision;
  *child_baton = cb;

  if (!copyfrom_path)
    {
      /* A simple add. */
      svn_kind_t *kind = apr_palloc(pb->eb->edit_pool, sizeof(*kind));

      *kind = svn_kind_dir;
      SVN_ERR(add_action(pb->eb, path, ACTION_ADD, kind));

      if (pb->copyfrom_path)
        {
          const char *name = svn_relpath_basename(path, result_pool);
          cb->copyfrom_path = apr_pstrcat(result_pool, pb->copyfrom_path,
                                          "/", name, NULL);
          cb->copyfrom_rev = pb->copyfrom_rev;
        }
    }
  else
    {
      /* A copy */
      struct copy_args *args = apr_palloc(pb->eb->edit_pool, sizeof(*args));

      args->copyfrom_path = apr_pstrdup(pb->eb->edit_pool, copyfrom_path);
      args->copyfrom_rev = copyfrom_revision;
      SVN_ERR(add_action(pb->eb, path, ACTION_COPY, args));

      cb->copyfrom_path = args->copyfrom_path;
      cb->copyfrom_rev = args->copyfrom_rev;
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
  struct ev2_dir_baton *db = apr_pcalloc(result_pool, sizeof(*db));

  db->eb = pb->eb;
  db->path = apr_pstrdup(result_pool, path);
  db->base_revision = base_revision;

  if (pb->copyfrom_path)
    {
      /* We are inside a copy. */
      const char *name = svn_relpath_basename(path, result_pool);

      db->copyfrom_path = apr_pstrcat(result_pool, pb->copyfrom_path,
                                      "/", name, NULL);
      db->copyfrom_rev = pb->copyfrom_rev;
    }

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
  p_args->base_revision = db->base_revision;
  p_args->kind = svn_kind_dir;

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
  struct ev2_file_baton *fb = apr_pcalloc(result_pool, sizeof(*fb));
  struct ev2_dir_baton *pb = parent_baton;

  fb->eb = pb->eb;
  fb->path = apr_pstrdup(result_pool, path);
  fb->base_revision = pb->base_revision;
  *file_baton = fb;

  if (!copyfrom_path)
    {
      /* A simple add. */
      svn_kind_t *kind = apr_palloc(pb->eb->edit_pool, sizeof(*kind));

      /* Don't bother fetching the base, as in an add we don't have a base. */
      fb->delta_base = NULL;

      *kind = svn_kind_file;
      SVN_ERR(add_action(pb->eb, path, ACTION_ADD, kind));
    }
  else
    {
      /* A copy */
      struct copy_args *args = apr_palloc(pb->eb->edit_pool, sizeof(*args));

      SVN_ERR(fb->eb->fetch_base_func(&fb->delta_base,
                                      fb->eb->fetch_base_baton,
                                      copyfrom_path, copyfrom_revision,
                                      result_pool, result_pool));

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
  struct ev2_file_baton *fb = apr_pcalloc(result_pool, sizeof(*fb));
  struct ev2_dir_baton *pb = parent_baton;

  fb->eb = pb->eb;
  fb->path = apr_pstrdup(result_pool, path);
  fb->base_revision = base_revision;

  if (pb->copyfrom_path)
    {
      /* We're in a copied directory, so the delta base is going to be
         based up on the copy source. */
      const char *name = svn_relpath_basename(path, result_pool);
      const char *copyfrom_path = apr_pstrcat(result_pool, pb->copyfrom_path,
                                              "/", name, NULL);

      SVN_ERR(fb->eb->fetch_base_func(&fb->delta_base,
                                      fb->eb->fetch_base_baton,
                                      copyfrom_path, pb->copyfrom_rev,
                                      result_pool, result_pool));
    }
  else
    {
      SVN_ERR(fb->eb->fetch_base_func(&fb->delta_base,
                                      fb->eb->fetch_base_baton,
                                      path, base_revision,
                                      result_pool, result_pool));
    }

  *file_baton = fb;
  return SVN_NO_ERROR;
}

struct handler_baton
{
  svn_txdelta_window_handler_t apply_handler;
  void *apply_baton;

  svn_stream_t *source;

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

  SVN_ERR(svn_stream_close(hb->source));

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
  svn_stream_t *target;
  struct path_checksum_args *pca = apr_pcalloc(fb->eb->edit_pool,
                                               sizeof(*pca));

  pca->base_revision = fb->base_revision;

  if (! fb->delta_base)
    hb->source = svn_stream_empty(handler_pool);
  else
    SVN_ERR(svn_stream_open_readonly(&hb->source, fb->delta_base, handler_pool,
                                     result_pool));

  SVN_ERR(svn_stream_open_unique(&target, &pca->path, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 fb->eb->edit_pool, result_pool));

  svn_txdelta_apply(hb->source, target,
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

  if (!strcmp(name, SVN_PROP_ENTRY_LOCK_TOKEN) && value == NULL)
    {
      /* We special case the lock token propery deletion, which is the
         server's way of telling the client to unlock the path. */
      SVN_ERR(add_action(fb->eb, fb->path, ACTION_UNLOCK, NULL));
    }

  /* We also pass through the deletion, since there may actually exist such
     a property we want to get rid of.   In the worse case, this is a no-op. */
  p_args->name = apr_pstrdup(fb->eb->edit_pool, name);
  p_args->value = value ? svn_string_dup(value, fb->eb->edit_pool) : NULL;
  p_args->base_revision = fb->base_revision;
  p_args->kind = svn_kind_file;

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
  eb->closed = TRUE;
  return svn_error_trace(svn_editor_complete(eb->editor));
}

static svn_error_t *
ev2_abort_edit(void *edit_baton,
               apr_pool_t *scratch_pool)
{
  struct ev2_edit_baton *eb = edit_baton;

  SVN_ERR(run_ev2_actions(edit_baton, scratch_pool));
  if (!eb->closed)
    return svn_error_trace(svn_editor_abort(eb->editor));
  else
    return SVN_NO_ERROR;
}

/* Return a svn_delta_editor_t * in DEDITOR, with an accompanying baton in
 * DEDITOR_BATON, which will be driven by EDITOR.  These will both be
 * allocated in RESULT_POOL, which may become large and long-lived;
 * SCRATCH_POOL is used for temporary allocations.
 *
 * The other parameters are as follows:
 *  - UNLOCK_FUNC / UNLOCK_BATON: A callback / baton which will be called
 *         when an unlocking action is received.
 *  - FOUND_ABS_PATHS: A pointer to a boolean flag which will be set if
 *         this shim determines that it is receiving absolute paths.
 *  - FETCH_PROPS_FUNC / FETCH_PROPS_BATON: A callback / baton pair which
 *         will be used by the shim handlers if they need to determine the
 *         existing properties on a  path.
 *  - FETCH_BASE_FUNC / FETCH_BASE_BATON: A callback / baton pair which will
 *         be used by the shims handlers if they need to determine the base
 *         text of a path.  It should only be invoked for files.
 *  - EXB: An 'extra baton' which is used to communicate between the shims.
 *         Its callbacks should be invoked at the appropriate time by this
 *         shim.
 */ 
static svn_error_t *
delta_from_editor(const svn_delta_editor_t **deditor,
                  void **dedit_baton,
                  svn_editor_t *editor,
                  unlock_func_t unlock_func,
                  void *unlock_baton,
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

  eb->do_unlock = unlock_func;
  eb->unlock_baton = unlock_baton;

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

  const char *path;
  svn_kind_t kind;  /* to copy, mkdir, put or set revprops */
  svn_revnum_t base_revision;       /* When committing, the base revision */
  svn_revnum_t copyfrom_revision;      /* to copy, valid for add and replace */
  svn_checksum_t *new_checksum;   /* An MD5 hash of the new contents, if any */
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
              svn_revnum_t base_revision,
              apr_pool_t *result_pool)
{
  struct operation *child = apr_hash_get(operation->children, path,
                                         APR_HASH_KEY_STRING);
  if (! child)
    {
      child = apr_pcalloc(result_pool, sizeof(*child));
      child->children = apr_hash_make(result_pool);
      child->path = apr_pstrdup(result_pool, path);
      child->operation = OP_OPEN;
      child->copyfrom_revision = SVN_INVALID_REVNUM;
      child->kind = svn_kind_dir;
      child->base_revision = base_revision;
      child->prop_mods = apr_hash_make(result_pool);
      child->prop_dels = apr_array_make(result_pool, 1, sizeof(const char *));
      apr_hash_set(operation->children, apr_pstrdup(result_pool, path),
                   APR_HASH_KEY_STRING, child);
    }

  /* If an operation has a child, it must of necessity be a directory,
     so ensure this fact. */
  operation->kind = svn_kind_dir;

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
      svn_checksum_t *checksum,
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
      operation = get_operation(path_so_far, operation, head, eb->edit_pool);
    }

  /* Handle property changes. */
  if (props)
    {
      apr_hash_t *current_props;
      apr_array_header_t *propdiffs;

      operation->kind = kind;

      if (operation->operation == OP_REPLACE)
        current_props = apr_hash_make(scratch_pool);
      else if (operation->copyfrom_url)
        SVN_ERR(eb->fetch_props_func(&current_props, eb->fetch_props_baton,
                                     operation->copyfrom_url,
                                     operation->copyfrom_revision,
                                     scratch_pool, scratch_pool));
      else
        SVN_ERR(eb->fetch_props_func(&current_props, eb->fetch_props_baton,
                                     relpath, rev, scratch_pool,
                                     scratch_pool));

      SVN_ERR(svn_prop_diffs(&propdiffs, props, current_props, scratch_pool));

      for (i = 0; i < propdiffs->nelts; i++)
        {
          /* Note: the array returned by svn_prop_diffs() is an array of
             actual structures, not pointers to them. */
          svn_prop_t *prop = &APR_ARRAY_IDX(propdiffs, i, svn_prop_t);
          if (!prop->value)
            APR_ARRAY_PUSH(operation->prop_dels, const char *) = 
                                        apr_pstrdup(eb->edit_pool, prop->name);
          else
            apr_hash_set(operation->prop_mods,
                         apr_pstrdup(eb->edit_pool, prop->name),
                         APR_HASH_KEY_STRING,
                         svn_string_dup(prop->value, eb->edit_pool));
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
    {
      operation->operation = OP_DELETE;
      operation->base_revision = rev;
    }

  else if (action == ACTION_ADD_ABSENT)
    operation->operation = OP_ADD_ABSENT;

  /* Handle copy operations (which can be adds or replacements). */
  else if (action == ACTION_COPY)
    {
      operation->operation =
        operation->operation == OP_DELETE ? OP_REPLACE : OP_ADD;

      if (kind == svn_kind_none || kind == svn_kind_unknown)
        SVN_ERR(eb->fetch_kind_func(&operation->kind, eb->fetch_kind_baton,
                                    url, rev, scratch_pool));
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
      else if (operation->operation == OP_OPEN)
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
      operation->src_file = apr_pstrdup(eb->edit_pool, src_file);
      operation->new_checksum = svn_checksum_dup(checksum, eb->edit_pool);
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

  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      /* We need to add the delete action. */

      SVN_ERR(build(eb, ACTION_DELETE, relpath, svn_kind_unknown,
                    NULL, SVN_INVALID_REVNUM,
                    NULL, NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));
    }

  SVN_ERR(build(eb, ACTION_MKDIR, relpath, svn_kind_dir,
                NULL, SVN_INVALID_REVNUM,
                NULL, NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));

  if (props && apr_hash_count(props) > 0)
    SVN_ERR(build(eb, ACTION_PROPSET, relpath, svn_kind_dir,
                  NULL, SVN_INVALID_REVNUM, props,
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
  struct editor_baton *eb = baton;
  const char *tmp_filename;
  svn_stream_t *tmp_stream;
  svn_checksum_t *md5_checksum;

  /* We may need to re-checksum these contents */
  if (!(checksum && checksum->kind == svn_checksum_md5))
    contents = svn_stream_checksummed2(contents, &md5_checksum, NULL,
                                       svn_checksum_md5, TRUE, scratch_pool);
  else
    md5_checksum = (svn_checksum_t *)checksum;

  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      /* We need to add the delete action. */

      SVN_ERR(build(eb, ACTION_DELETE, relpath, svn_kind_unknown,
                    NULL, SVN_INVALID_REVNUM,
                    NULL, NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));
    }

  /* Spool the contents to a tempfile, and provide that to the driver. */
  SVN_ERR(svn_stream_open_unique(&tmp_stream, &tmp_filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 eb->edit_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(contents, tmp_stream, NULL, NULL, scratch_pool));

  SVN_ERR(build(eb, ACTION_PUT, relpath, svn_kind_none,
                NULL, SVN_INVALID_REVNUM,
                NULL, tmp_filename, md5_checksum, SVN_INVALID_REVNUM,
                scratch_pool));

  if (props && apr_hash_count(props) > 0)
    SVN_ERR(build(eb, ACTION_PROPSET, relpath, svn_kind_file,
                  NULL, SVN_INVALID_REVNUM, props,
                  NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));

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

  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      /* We need to add the delete action. */

      SVN_ERR(build(eb, ACTION_DELETE, relpath, svn_kind_unknown,
                    NULL, SVN_INVALID_REVNUM,
                    NULL, NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));
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

  SVN_ERR(build(eb, ACTION_ADD_ABSENT, relpath, kind,
                NULL, SVN_INVALID_REVNUM,
                NULL, NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));

  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_alter_directory_t */
static svn_error_t *
alter_directory_cb(void *baton,
                   const char *relpath,
                   svn_revnum_t revision,
                   apr_hash_t *props,
                   apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;

  /* ### should we verify the kind is truly a directory?  */

  SVN_ERR(build(eb, ACTION_PROPSET, relpath, svn_kind_dir,
                NULL, SVN_INVALID_REVNUM,
                props, NULL, NULL, revision, scratch_pool));

  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_alter_file_t */
static svn_error_t *
alter_file_cb(void *baton,
              const char *relpath,
              svn_revnum_t revision,
              apr_hash_t *props,
              const svn_checksum_t *checksum,
              svn_stream_t *contents,
              apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;
  const char *tmp_filename;
  svn_stream_t *tmp_stream;
  svn_checksum_t *md5_checksum;

  /* ### should we verify the kind is truly a file?  */

  if (contents)
    {
      /* We may need to re-checksum these contents */
      if (!(checksum && checksum->kind == svn_checksum_md5))
        contents = svn_stream_checksummed2(contents, &md5_checksum, NULL,
                                           svn_checksum_md5, TRUE,
                                           scratch_pool);
      else
        md5_checksum = (svn_checksum_t *)checksum;

      /* Spool the contents to a tempfile, and provide that to the driver. */
      SVN_ERR(svn_stream_open_unique(&tmp_stream, &tmp_filename, NULL,
                                     svn_io_file_del_on_pool_cleanup,
                                     eb->edit_pool, scratch_pool));
      SVN_ERR(svn_stream_copy3(contents, tmp_stream, NULL, NULL,
                               scratch_pool));

      SVN_ERR(build(eb, ACTION_PUT, relpath, svn_kind_file,
                    NULL, SVN_INVALID_REVNUM,
                    NULL, tmp_filename, md5_checksum, revision, scratch_pool));
    }

  if (props)
    {
      SVN_ERR(build(eb, ACTION_PROPSET, relpath, svn_kind_file,
                    NULL, SVN_INVALID_REVNUM,
                    props, NULL, NULL, revision, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_alter_symlink_t */
static svn_error_t *
alter_symlink_cb(void *baton,
                 const char *relpath,
                 svn_revnum_t revision,
                 apr_hash_t *props,
                 const char *target,
                 apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;

  /* ### should we verify the kind is truly a symlink?  */

  /* ### do something  */

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

  SVN_ERR(build(eb, ACTION_DELETE, relpath, svn_kind_unknown,
                NULL, revision, NULL, NULL, NULL, SVN_INVALID_REVNUM,
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

  if (SVN_IS_VALID_REVNUM(replaces_rev))
    {
      /* We need to add the delete action. */

      SVN_ERR(build(eb, ACTION_DELETE, dst_relpath, svn_kind_unknown,
                    NULL, SVN_INVALID_REVNUM,
                    NULL, NULL, NULL, SVN_INVALID_REVNUM, scratch_pool));
    }

  SVN_ERR(build(eb, ACTION_COPY, dst_relpath, svn_kind_unknown,
                src_relpath, src_revision, NULL, NULL, NULL,
                SVN_INVALID_REVNUM, scratch_pool));

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

  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_rotate_t */
static svn_error_t *
rotate_cb(void *baton,
          const apr_array_header_t *relpaths,
          const apr_array_header_t *revisions,
          apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;

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
drive_tree(struct operation *op,
           const struct operation *parent_op,
           const svn_delta_editor_t *editor,
           svn_boolean_t make_abs_paths,
           apr_pool_t *scratch_pool)
{
  const char *path = op->path;

  if (path[0] != '/' && make_abs_paths)
    path = apr_pstrcat(scratch_pool, "/", path, NULL);

  /* Deletes and replacements are simple -- just delete the thing. */
  if (op->operation == OP_DELETE || op->operation == OP_REPLACE)
    {
      SVN_ERR(editor->delete_entry(path, op->base_revision,
                                   parent_op->baton, scratch_pool));
    }

  if (op->kind == svn_kind_dir)
    {
      /* Open or create our baton. */
      if (op->operation == OP_OPEN || op->operation == OP_PROPSET)
        SVN_ERR(editor->open_directory(path, parent_op->baton, op->base_revision,
                                       scratch_pool, &op->baton));

      else if (op->operation == OP_ADD || op->operation == OP_REPLACE)
        SVN_ERR(editor->add_directory(path, parent_op->baton,
                                      op->copyfrom_url, op->copyfrom_revision,
                                      scratch_pool, &op->baton));

      else if (op->operation == OP_ADD_ABSENT)
        SVN_ERR(editor->absent_directory(path, parent_op->baton,
                                         scratch_pool));

      if (op->baton)
        {
          apr_pool_t *iterpool = svn_pool_create(scratch_pool);
          apr_hash_index_t *hi;

          /* Do any prop mods we may have. */
          SVN_ERR(change_props(editor, op->baton, op, scratch_pool));

          for (hi = apr_hash_first(scratch_pool, op->children);
               hi; hi = apr_hash_next(hi))
            {
              struct operation *child = svn__apr_hash_index_val(hi);

              svn_pool_clear(iterpool);
              SVN_ERR(drive_tree(child, op, editor, make_abs_paths, iterpool));
            }
          svn_pool_destroy(iterpool);

          /* We're done, close the directory. */
          SVN_ERR(editor->close_directory(op->baton, scratch_pool));
        }
    }
  else
    {
      /* This currently treats anything that isn't a directory as a file.
         I don't know that that's a valid assumption... */

      void *file_baton = NULL;
      
      /* Open or create our baton. */
      if (op->operation == OP_OPEN || op->operation == OP_PROPSET)
        SVN_ERR(editor->open_file(path, parent_op->baton, op->base_revision,
                                  scratch_pool, &file_baton));

      else if (op->operation == OP_ADD || op->operation == OP_REPLACE)
        SVN_ERR(editor->add_file(path, parent_op->baton, op->copyfrom_url,
                                 op->copyfrom_revision, scratch_pool,
                                 &file_baton));

      else if (op->operation == OP_ADD_ABSENT)
        SVN_ERR(editor->absent_file(path, parent_op->baton, scratch_pool));

      if (file_baton)
        {
          /* Do we need to change text contents? */
          if (op->src_file)
            {
              svn_txdelta_window_handler_t handler;
              void *handler_baton;
              svn_stream_t *contents;

              SVN_ERR(editor->apply_textdelta(file_baton, NULL, scratch_pool,
                                              &handler, &handler_baton));
              SVN_ERR(svn_stream_open_readonly(&contents, op->src_file,
                                               scratch_pool, scratch_pool));
              SVN_ERR(svn_txdelta_send_stream(contents, handler, handler_baton,
                                              NULL, scratch_pool));
              SVN_ERR(svn_stream_close(contents));
            }

          /* Do any prop mods we may have. */
          SVN_ERR(change_props(editor, file_baton, op, scratch_pool));

          /* Close the file. */
          SVN_ERR(editor->close_file(file_baton,
                                     svn_checksum_to_cstring(op->new_checksum,
                                                             scratch_pool),
                                     scratch_pool));
        }

    }

  return SVN_NO_ERROR;
}

/* This is a special case of drive_tree(), meant to handle the root, which
   doesn't have a parent and should already be open. */
static svn_error_t *
drive_root(struct operation *root,
           const svn_delta_editor_t *editor,
           svn_boolean_t make_abs_paths,
           apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Early out: if we haven't opened the root yet (which would usually only
     be the case in an abort), there isn't much we can do here. */
  if (!root->baton)
    return SVN_NO_ERROR;

  /* Do any prop mods we may have. */
  SVN_ERR(change_props(editor, root->baton, root, scratch_pool));

  /* Now iterate over our children. */
  for (hi = apr_hash_first(scratch_pool, root->children);
       hi; hi = apr_hash_next(hi))
    {
      struct operation *child = svn__apr_hash_index_val(hi);

      svn_pool_clear(iterpool);
      SVN_ERR(drive_tree(child, root, editor, make_abs_paths, iterpool));
    }
  
  /* We need to close the root directory, but leave it to our caller to call
     close_ or abort_edit(). */
  SVN_ERR(editor->close_directory(root->baton, scratch_pool));

  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_complete_t */
static svn_error_t *
complete_cb(void *baton,
            apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;
  svn_error_t *err;

  /* Drive the tree we've created. */
  err = drive_root(&eb->root, eb->deditor, *eb->make_abs_paths, scratch_pool);
  if (!err)
     {
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
  err = drive_root(&eb->root, eb->deditor, *eb->make_abs_paths, scratch_pool);

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

  eb->root.base_revision = base_revision;
  SVN_ERR(eb->deditor->open_root(eb->dedit_baton, eb->root.base_revision,
                                 eb->edit_pool, &eb->root.baton));

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
do_unlock(void *baton,
          const char *path,
          apr_pool_t *scratch_pool)
{
  struct editor_baton *eb = baton;
  apr_array_header_t *path_bits = svn_path_decompose(path, scratch_pool);
  const char *path_so_far = "";
  struct operation *operation = &eb->root;
  int i;

  /* Look for any previous operations we've recognized for PATH.  If
     any of PATH's ancestors have not yet been traversed, we'll be
     creating OP_OPEN operations for them as we walk down PATH's path
     components. */
  for (i = 0; i < path_bits->nelts; ++i)
    {
      const char *path_bit = APR_ARRAY_IDX(path_bits, i, const char *);
      path_so_far = svn_relpath_join(path_so_far, path_bit, scratch_pool);
      operation = get_operation(path_so_far, operation, SVN_INVALID_REVNUM,
                                eb->edit_pool);
    }

  APR_ARRAY_PUSH(operation->prop_dels, const char *) =
                                                SVN_PROP_ENTRY_LOCK_TOKEN;

  return SVN_NO_ERROR;
}

/* Return an svn_editor_t * in EDITOR_P which will be driven by
 * DEDITOR/DEDIT_BATON.  EDITOR_P is allocated in RESULT_POOL, which may
 * become large and long-lived; SCRATCH_POOL is used for temporary
 * allocations.
 *
 * The other parameters are as follows:
 *  - EXB: An 'extra_baton' used for passing information between the coupled
 *         shims.  This includes actions like 'start edit' and 'set target'.
 *         As this shim receives these actions, it provides the extra baton
 *         to its caller.
 *  - UNLOCK_FUNC / UNLOCK_BATON: A callback / baton pair which a caller
 *         can use to notify this shim that a path should be unlocked (in the
 *         'svn lock' sense).  As this shim receives this action, it provides
 *         this callback / baton to its caller.
 *  - SEND_ABS_PATHS: A pointer which will be set prior to this edit (but
 *         not necessarily at the invocation of editor_from_delta()),and
 *         which indicates whether incoming paths should be expected to
 *         be absolute or relative.
 *  - CANCEL_FUNC / CANCEL_BATON: The usual; folded into the produced editor.
 *  - FETCH_KIND_FUNC / FETCH_KIND_BATON: A callback / baton pair which will
 *         be used by the shim handlers if they need to determine the kind of
 *         a path.
 *  - FETCH_PROPS_FUNC / FETCH_PROPS_BATON: A callback / baton pair which
 *         will be used by the shim handlers if they need to determine the
 *         existing properties on a path.
 */
static svn_error_t *
editor_from_delta(svn_editor_t **editor_p,
                  struct extra_baton **exb,
                  unlock_func_t *unlock_func,
                  void **unlock_baton,
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
      alter_directory_cb,
      alter_file_cb,
      alter_symlink_cb,
      delete_cb,
      copy_cb,
      move_cb,
      rotate_cb,
      complete_cb,
      abort_cb
    };
  struct editor_baton *eb = apr_pcalloc(result_pool, sizeof(*eb));
  struct extra_baton *extra_baton = apr_pcalloc(result_pool,
                                                sizeof(*extra_baton));

  eb->deditor = deditor;
  eb->dedit_baton = dedit_baton;
  eb->edit_pool = result_pool;
  eb->paths = apr_hash_make(result_pool);

  eb->fetch_kind_func = fetch_kind_func;
  eb->fetch_kind_baton = fetch_kind_baton;
  eb->fetch_props_func = fetch_props_func;
  eb->fetch_props_baton = fetch_props_baton;

  eb->root.path = NULL;
  eb->root.children = apr_hash_make(result_pool);
  eb->root.kind = svn_kind_dir;
  eb->root.operation = OP_OPEN;
  eb->root.prop_mods = apr_hash_make(result_pool);
  eb->root.prop_dels = apr_array_make(result_pool, 1, sizeof(const char *));
  eb->root.copyfrom_revision = SVN_INVALID_REVNUM;

  eb->make_abs_paths = send_abs_paths;

  SVN_ERR(svn_editor_create(&editor, eb, cancel_func, cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(editor, &editor_cbs, scratch_pool));

  *editor_p = editor;

  *unlock_func = do_unlock;
  *unlock_baton = eb;

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

  unlock_func_t unlock_func;
  void *unlock_baton;

  SVN_ERR_ASSERT(shim_callbacks->fetch_kind_func != NULL);
  SVN_ERR_ASSERT(shim_callbacks->fetch_props_func != NULL);
  SVN_ERR_ASSERT(shim_callbacks->fetch_base_func != NULL);

  SVN_ERR(editor_from_delta(&editor, &exb, &unlock_func, &unlock_baton,
                            deditor_in, dedit_baton_in,
                            found_abs_paths, NULL, NULL,
                            shim_callbacks->fetch_kind_func,
                            shim_callbacks->fetch_baton,
                            shim_callbacks->fetch_props_func,
                            shim_callbacks->fetch_baton,
                            result_pool, scratch_pool));
  SVN_ERR(delta_from_editor(deditor_out, dedit_baton_out, editor,
                            unlock_func, unlock_baton,
                            found_abs_paths,
                            shim_callbacks->fetch_props_func,
                            shim_callbacks->fetch_baton,
                            shim_callbacks->fetch_base_func,
                            shim_callbacks->fetch_baton,
                            exb, result_pool));

#endif
  return SVN_NO_ERROR;
}
