/*
 * editor.c :  editing trees of versioned resources
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
#include "svn_pools.h"
#include "svn_editor.h"
#include "svn_dirent_uri.h"

#ifdef SVN_DEBUG
/* This enables runtime checks of the editor API constraints.  This may
   introduce additional memory and runtime overhead, and should not be used
   in production builds.
   
   ### Remove before release? */
#define ENABLE_ORDERING_CHECK
#endif


struct svn_editor_t
{
  void *baton;

  /* Standard cancellation function. Called before each callback.  */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* Our callback functions match that of the set-many structure, so
     just use that.  */
  svn_editor_cb_many_t funcs;

  /* This pool is used as the scratch_pool for all callbacks.  */
  apr_pool_t *scratch_pool;

#ifdef ENABLE_ORDERING_CHECK
  apr_hash_t *pending_incomplete_children;
  apr_hash_t *completed_nodes;
  svn_boolean_t finished;

  apr_pool_t *result_pool;
#endif
};

#ifdef ENABLE_ORDERING_CHECK

/* Marker to indicate no further changes are allowed on this node.  */
static const int marker_done;
#define MARKER_DONE (&marker_done)

/* Marker indicating that add_* may be called for this path, or that it
   can be the destination of a copy or move. For copy/move, the path
   will switch to MARKER_ALLOW_ALTER, to enable further tweaks.  */
static const int marker_allow_add;
#define MARKER_ALLOW_ADD (&marker_allow_add)

/* Marker indicating that alter_* may be called for this path.  */
static const int marker_allow_alter;
#define MARKER_ALLOW_ALTER (&marker_allow_alter)

/* Just like MARKER_DONE, but also indicates that the node was created
   via add_directory(). This allows us to verify that the CHILDREN param
   was comprehensive.  */
static const int marker_added_dir;
#define MARKER_ADDED_DIR (&marker_added_dir)

#define MARK_FINISHED(editor) ((editor)->finished = TRUE)
#define SHOULD_NOT_BE_FINISHED(editor)  SVN_ERR_ASSERT(!(editor)->finished)

#define CLEAR_INCOMPLETE(editor, relpath) \
  apr_hash_set((editor)->pending_incomplete_children, relpath,  \
               APR_HASH_KEY_STRING, NULL);

#define MARK_RELPATH(editor, relpath, value) \
  apr_hash_set((editor)->completed_nodes, \
               apr_pstrdup((editor)->result_pool, relpath), \
               APR_HASH_KEY_STRING, value)

#define MARK_COMPLETED(editor, relpath) \
  MARK_RELPATH(editor, relpath, MARKER_DONE)
#define SHOULD_NOT_BE_COMPLETED(editor, relpath) \
  SVN_ERR_ASSERT(apr_hash_get((editor)->completed_nodes, relpath, \
                              APR_HASH_KEY_STRING) == NULL)

#define MARK_ALLOW_ADD(editor, relpath) \
  MARK_RELPATH(editor, relpath, MARKER_ALLOW_ADD)
#define SHOULD_ALLOW_ADD(editor, relpath) \
  SVN_ERR_ASSERT(allow_either(editor, relpath, MARKER_ALLOW_ADD, NULL))

#define MARK_ALLOW_ALTER(editor, relpath) \
  MARK_RELPATH(editor, relpath, MARKER_ALLOW_ALTER)
#define SHOULD_ALLOW_ALTER(editor, relpath) \
  SVN_ERR_ASSERT(allow_either(editor, relpath, MARKER_ALLOW_ALTER, NULL))

#define MARK_ADDED_DIR(editor, relpath) \
  MARK_RELPATH(editor, relpath, MARKER_ADDED_DIR)
#define CHECK_UNKNOWN_CHILD(editor, relpath) \
  SVN_ERR_ASSERT(check_unknown_child(editor, relpath))

static svn_boolean_t
allow_either(const svn_editor_t *editor,
             const char *relpath,
             const void *marker1,
             const void *marker2)
{
  void *value = apr_hash_get(editor->completed_nodes, relpath,
                             APR_HASH_KEY_STRING);
  return value == marker1 || value == marker2;
}

static svn_boolean_t
check_unknown_child(const svn_editor_t *editor,
                    const char *relpath)
{
  const char *parent;

  /* If we already know about the new child, then exit early.  */
  if (apr_hash_get(editor->pending_incomplete_children, relpath,
                   APR_HASH_KEY_STRING) != NULL)
    return TRUE;

  parent = svn_relpath_dirname(relpath, editor->scratch_pool);

  /* Was this parent created via svn_editor_add_directory() ?  */
  if (apr_hash_get(editor->completed_nodes, parent, APR_HASH_KEY_STRING)
      == MARKER_ADDED_DIR)
    {
      /* Whoops. This child should have been listed in that add call,
         and placed into ->pending_incomplete_children.  */
      return FALSE;
    }

  /* The parent was not added in this drive.  */
  return TRUE;
}

#else

/* Be wary with the definition of these macros so that we don't
   end up with "statement with no effect" warnings. Obviously, this
   depends upon particular usage, which is easy to verify.  */

#define MARK_FINISHED(editor)  /* empty */
#define SHOULD_NOT_BE_FINISHED(editor)  /* empty */

#define CLEAR_INCOMPLETE(editor, relpath)  /* empty */

#define MARK_COMPLETED(editor, relpath)  /* empty */
#define SHOULD_NOT_BE_COMPLETED(editor, relpath)  /* empty */

#define MARK_ALLOW_ADD(editor, relpath)  /* empty */
#define SHOULD_ALLOW_ADD(editor, relpath)  /* empty */

#define MARK_ALLOW_ALTER(editor, relpath)  /* empty */
#define SHOULD_ALLOW_ALTER(editor, relpath)  /* empty */

#define MARK_ADDED_DIR(editor, relpath)  /* empty */
#define CHECK_UNKNOWN_CHILD(editor, relpath)  /* empty */

#endif /* ENABLE_ORDERING_CHECK */


svn_error_t *
svn_editor_create(svn_editor_t **editor,
                  void *editor_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  *editor = apr_pcalloc(result_pool, sizeof(**editor));

  (*editor)->baton = editor_baton;
  (*editor)->cancel_func = cancel_func;
  (*editor)->cancel_baton = cancel_baton;
  (*editor)->scratch_pool = svn_pool_create(result_pool);

#ifdef ENABLE_ORDERING_CHECK
  (*editor)->pending_incomplete_children = apr_hash_make(result_pool);
  (*editor)->completed_nodes = apr_hash_make(result_pool);
  (*editor)->finished = FALSE;
  (*editor)->result_pool = result_pool;
#endif

  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_add_directory(svn_editor_t *editor,
                               svn_editor_cb_add_directory_t callback,
                               apr_pool_t *scratch_pool)
{
  editor->funcs.cb_add_directory = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_add_file(svn_editor_t *editor,
                          svn_editor_cb_add_file_t callback,
                          apr_pool_t *scratch_pool)
{
  editor->funcs.cb_add_file = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_add_symlink(svn_editor_t *editor,
                             svn_editor_cb_add_symlink_t callback,
                             apr_pool_t *scratch_pool)
{
  editor->funcs.cb_add_symlink = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_add_absent(svn_editor_t *editor,
                            svn_editor_cb_add_absent_t callback,
                            apr_pool_t *scratch_pool)
{
  editor->funcs.cb_add_absent = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_alter_directory(svn_editor_t *editor,
                                 svn_editor_cb_alter_directory_t callback,
                                 apr_pool_t *scratch_pool)
{
  editor->funcs.cb_alter_directory = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_alter_file(svn_editor_t *editor,
                            svn_editor_cb_alter_file_t callback,
                            apr_pool_t *scratch_pool)
{
  editor->funcs.cb_alter_file = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_alter_symlink(svn_editor_t *editor,
                               svn_editor_cb_alter_symlink_t callback,
                               apr_pool_t *scratch_pool)
{
  editor->funcs.cb_alter_symlink = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_delete(svn_editor_t *editor,
                        svn_editor_cb_delete_t callback,
                        apr_pool_t *scratch_pool)
{
  editor->funcs.cb_delete = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_copy(svn_editor_t *editor,
                      svn_editor_cb_copy_t callback,
                      apr_pool_t *scratch_pool)
{
  editor->funcs.cb_copy = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_move(svn_editor_t *editor,
                      svn_editor_cb_move_t callback,
                      apr_pool_t *scratch_pool)
{
  editor->funcs.cb_move = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_rotate(svn_editor_t *editor,
                        svn_editor_cb_rotate_t callback,
                        apr_pool_t *scratch_pool)
{
  editor->funcs.cb_rotate = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_complete(svn_editor_t *editor,
                          svn_editor_cb_complete_t callback,
                          apr_pool_t *scratch_pool)
{
  editor->funcs.cb_complete = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_abort(svn_editor_t *editor,
                       svn_editor_cb_abort_t callback,
                       apr_pool_t *scratch_pool)
{
  editor->funcs.cb_abort = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_many(svn_editor_t *editor,
                      const svn_editor_cb_many_t *many,
                      apr_pool_t *scratch_pool)
{
#define COPY_CALLBACK(NAME) if (many->NAME) editor->funcs.NAME = many->NAME

  COPY_CALLBACK(cb_add_directory);
  COPY_CALLBACK(cb_add_file);
  COPY_CALLBACK(cb_add_symlink);
  COPY_CALLBACK(cb_add_absent);
  COPY_CALLBACK(cb_alter_directory);
  COPY_CALLBACK(cb_alter_file);
  COPY_CALLBACK(cb_alter_symlink);
  COPY_CALLBACK(cb_delete);
  COPY_CALLBACK(cb_copy);
  COPY_CALLBACK(cb_move);
  COPY_CALLBACK(cb_rotate);
  COPY_CALLBACK(cb_complete);
  COPY_CALLBACK(cb_abort);

#undef COPY_CALLBACK

  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_add_directory(svn_editor_t *editor,
                         const char *relpath,
                         const apr_array_header_t *children,
                         apr_hash_t *props,
                         svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(children != NULL);
  SVN_ERR_ASSERT(props != NULL);
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ADD(editor, relpath);
  CHECK_UNKNOWN_CHILD(editor, relpath);

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_add_directory)
    err = editor->funcs.cb_add_directory(editor->baton, relpath, children,
                                         props, replaces_rev,
                                         editor->scratch_pool);

  MARK_ADDED_DIR(editor, relpath);
  CLEAR_INCOMPLETE(editor, relpath);

#ifdef ENABLE_ORDERING_CHECK
  {
    int i;
    for (i = 0; i < children->nelts; i++)
      {
        const char *child_basename = APR_ARRAY_IDX(children, i, const char *);
        const char *child = svn_relpath_join(relpath, child_basename,
                                             editor->result_pool);

        apr_hash_set(editor->pending_incomplete_children, child,
                     APR_HASH_KEY_STRING, "");
      }
  }
#endif

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_add_file(svn_editor_t *editor,
                    const char *relpath,
                    const svn_checksum_t *checksum,
                    svn_stream_t *contents,
                    apr_hash_t *props,
                    svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(checksum != NULL);
  SVN_ERR_ASSERT(contents != NULL);
  SVN_ERR_ASSERT(props != NULL);
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ADD(editor, relpath);
  CHECK_UNKNOWN_CHILD(editor, relpath);

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_add_file)
    err = editor->funcs.cb_add_file(editor->baton, relpath,
                                    checksum, contents, props,
                                    replaces_rev, editor->scratch_pool);

  MARK_COMPLETED(editor, relpath);
  CLEAR_INCOMPLETE(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_add_symlink(svn_editor_t *editor,
                       const char *relpath,
                       const char *target,
                       apr_hash_t *props,
                       svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(props != NULL);
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ADD(editor, relpath);
  CHECK_UNKNOWN_CHILD(editor, relpath);

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_add_symlink)
    err = editor->funcs.cb_add_symlink(editor->baton, relpath, target, props,
                                       replaces_rev, editor->scratch_pool);

  MARK_COMPLETED(editor, relpath);
  CLEAR_INCOMPLETE(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_add_absent(svn_editor_t *editor,
                      const char *relpath,
                      svn_kind_t kind,
                      svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ADD(editor, relpath);
  CHECK_UNKNOWN_CHILD(editor, relpath);

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_add_absent)
    err = editor->funcs.cb_add_absent(editor->baton, relpath, kind,
                                      replaces_rev, editor->scratch_pool);

  MARK_COMPLETED(editor, relpath);
  CLEAR_INCOMPLETE(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_alter_directory(svn_editor_t *editor,
                           const char *relpath,
                           svn_revnum_t revision,
                           apr_hash_t *props)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(props != NULL);
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ALTER(editor, relpath);

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_alter_directory)
    err = editor->funcs.cb_alter_directory(editor->baton,
                                           relpath, revision, props,
                                           editor->scratch_pool);

  MARK_COMPLETED(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_alter_file(svn_editor_t *editor,
                      const char *relpath,
                      svn_revnum_t revision,
                      apr_hash_t *props,
                      const svn_checksum_t *checksum,
                      svn_stream_t *contents)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT((checksum != NULL && contents != NULL)
                 || (checksum == NULL && contents == NULL));
  SVN_ERR_ASSERT(props != NULL || checksum != NULL);
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ALTER(editor, relpath);

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_alter_file)
    err = editor->funcs.cb_alter_file(editor->baton,
                                      relpath, revision, props,
                                      checksum, contents,
                                      editor->scratch_pool);

  MARK_COMPLETED(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_alter_symlink(svn_editor_t *editor,
                         const char *relpath,
                         svn_revnum_t revision,
                         apr_hash_t *props,
                         const char *target)
{
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(props != NULL || target != NULL);
  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ALTER(editor, relpath);

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_alter_symlink)
    err = editor->funcs.cb_alter_symlink(editor->baton,
                                         relpath, revision, props,
                                         target,
                                         editor->scratch_pool);

  MARK_COMPLETED(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_delete(svn_editor_t *editor,
                  const char *relpath,
                  svn_revnum_t revision)
{
  svn_error_t *err = SVN_NO_ERROR;

  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_NOT_BE_COMPLETED(editor, relpath);

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_delete)
    err = editor->funcs.cb_delete(editor->baton, relpath, revision,
                                  editor->scratch_pool);

  MARK_COMPLETED(editor, relpath);

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_copy(svn_editor_t *editor,
                const char *src_relpath,
                svn_revnum_t src_revision,
                const char *dst_relpath,
                svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_ALLOW_ADD(editor, dst_relpath);

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_copy)
    err = editor->funcs.cb_copy(editor->baton, src_relpath, src_revision,
                                dst_relpath, replaces_rev,
                                editor->scratch_pool);

  MARK_ALLOW_ALTER(editor, dst_relpath);
  CLEAR_INCOMPLETE(editor, dst_relpath);

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_move(svn_editor_t *editor,
                const char *src_relpath,
                svn_revnum_t src_revision,
                const char *dst_relpath,
                svn_revnum_t replaces_rev)
{
  svn_error_t *err = SVN_NO_ERROR;

  SHOULD_NOT_BE_FINISHED(editor);
  SHOULD_NOT_BE_COMPLETED(editor, src_relpath);
  SHOULD_ALLOW_ADD(editor, dst_relpath);

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_move)
    err = editor->funcs.cb_move(editor->baton, src_relpath, src_revision,
                                dst_relpath, replaces_rev,
                                editor->scratch_pool);

  MARK_ALLOW_ADD(editor, src_relpath);
  MARK_ALLOW_ALTER(editor, dst_relpath);
  CLEAR_INCOMPLETE(editor, dst_relpath);

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_rotate(svn_editor_t *editor,
                  const apr_array_header_t *relpaths,
                  const apr_array_header_t *revisions)
{
  svn_error_t *err = SVN_NO_ERROR;

  SHOULD_NOT_BE_FINISHED(editor);
#ifdef ENABLE_ORDERING_CHECK
  {
    int i;
    for (i = 0; i < relpaths->nelts; i++)
      {
        const char *relpath = APR_ARRAY_IDX(relpaths, i, const char *);
        SHOULD_NOT_BE_COMPLETED(editor, relpath);
      }
  }
#endif

  if (editor->cancel_func)
    SVN_ERR(editor->cancel_func(editor->cancel_baton));

  if (editor->funcs.cb_rotate)
    err = editor->funcs.cb_rotate(editor->baton, relpaths, revisions,
                                  editor->scratch_pool);

#ifdef ENABLE_ORDERING_CHECK
  {
    int i;
    for (i = 0; i < relpaths->nelts; i++)
      {
        const char *relpath = APR_ARRAY_IDX(relpaths, i, const char *);
        MARK_ALLOW_ALTER(editor, relpath);
      }
  }
#endif

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_complete(svn_editor_t *editor)
{
  svn_error_t *err = SVN_NO_ERROR;

  SHOULD_NOT_BE_FINISHED(editor);
#ifdef ENABLE_ORDERING_CHECK
  SVN_ERR_ASSERT(apr_hash_count(editor->pending_incomplete_children) == 0);
#endif

  if (editor->funcs.cb_complete)
    err = editor->funcs.cb_complete(editor->baton, editor->scratch_pool);

  MARK_FINISHED(editor);

  svn_pool_clear(editor->scratch_pool);
  return err;
}


svn_error_t *
svn_editor_abort(svn_editor_t *editor)
{
  svn_error_t *err = SVN_NO_ERROR;

  SHOULD_NOT_BE_FINISHED(editor);

  if (editor->funcs.cb_abort)
    err = editor->funcs.cb_abort(editor->baton, editor->scratch_pool);

  MARK_FINISHED(editor);

  svn_pool_clear(editor->scratch_pool);
  return err;
}
