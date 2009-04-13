/*
 * editor.c :  editing trees of versioned resources
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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

#include "svn_types.h"
#include "svn_error.h"
#include "svn_editor.h"


struct svn_editor_t
{
  void *baton;

  /* Our callback functions match that of the set-many structure, so
     just use that.  */
  svn_editor_cb_many_t funcs;

};


svn_error_t *
svn_editor_create(svn_editor_t **editor,
                  void *baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  *editor = apr_pcalloc(result_pool, sizeof(**editor));
  (*editor)->baton = baton;
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
svn_editor_setcb_add_directory_streamy(
  svn_editor_t *editor,
  svn_editor_cb_add_directory_streamy_t callback,
  apr_pool_t *scratch_pool)
{
  editor->funcs.cb_add_directory_streamy = callback;
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
svn_editor_setcb_set_props(svn_editor_t *editor,
                           svn_editor_cb_set_props_t callback,
                           apr_pool_t *scratch_pool)
{
  editor->funcs.cb_set_props = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_set_text(svn_editor_t *editor,
                          svn_editor_cb_set_text_t callback,
                          apr_pool_t *scratch_pool)
{
  editor->funcs.cb_set_text = callback;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_editor_setcb_set_target(svn_editor_t *editor,
                            svn_editor_cb_set_target_t callback,
                            apr_pool_t *scratch_pool)
{
  editor->funcs.cb_set_target = callback;
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
  COPY_CALLBACK(cb_add_directory_streamy);
  COPY_CALLBACK(cb_add_file);
  COPY_CALLBACK(cb_add_symlink);
  COPY_CALLBACK(cb_add_absent);
  COPY_CALLBACK(cb_set_props);
  COPY_CALLBACK(cb_set_text);
  COPY_CALLBACK(cb_set_target);
  COPY_CALLBACK(cb_delete);
  COPY_CALLBACK(cb_copy);
  COPY_CALLBACK(cb_move);
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
                         apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_add_directory)(editor->baton, relpath, children,
                                           props, scratch_pool);
}


svn_error_t *
svn_editor_add_directory_streamy(svn_editor_t *editor,
                                 const char *relpath,
                                 svn_stream_t *children,
                                 apr_hash_t *props,
                                 apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_add_directory_streamy)(editor->baton, relpath,
                                                   children, props,
                                                   scratch_pool);
}


svn_error_t *
svn_editor_add_file(svn_editor_t *editor,
                    const char *relpath,
                    apr_hash_t *props,
                    apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_add_file)(editor->baton, relpath, props,
                                      scratch_pool);
}


svn_error_t *
svn_editor_add_symlink(svn_editor_t *editor,
                       const char *relpath,
                       const char *target,
                       apr_hash_t *props,
                       apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_add_symlink)(editor->baton, relpath, target, props,
                                         scratch_pool);
}


svn_error_t *
svn_editor_add_absent(svn_editor_t *editor,
                      const char *relpath,
                      svn_node_kind_t kind,
                      apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_add_absent)(editor->baton, relpath, kind,
                                        scratch_pool);
}


svn_error_t *
svn_editor_set_props(svn_editor_t *editor,
                     const char *relpath,
                     svn_revnum_t revision,
                     apr_hash_t *props,
                     svn_boolean_t complete,
                     apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_set_props)(editor->baton, relpath, revision, props,
                                       complete, scratch_pool);
}


svn_error_t *
svn_editor_set_text(svn_editor_t *editor,
                    const char *relpath,
                    svn_revnum_t revision,
                    const svn_checksum_t *checksum,
                    svn_stream_t *contents,
                    apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_set_text)(editor->baton, relpath, revision,
                                      checksum, contents, scratch_pool);
}


svn_error_t *
svn_editor_set_target(svn_editor_t *editor,
                      const char *relpath,
                      svn_revnum_t revision,
                      const char *target,
                      apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_set_target)(editor->baton, relpath, revision,
                                        target, scratch_pool);
}


svn_error_t *
svn_editor_delete(svn_editor_t *editor,
                  const char *relpath,
                  svn_revnum_t revision,
                  apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_delete)(editor->baton, relpath, revision,
                                    scratch_pool);
}


svn_error_t *
svn_editor_copy(svn_editor_t *editor,
                const char *src_relpath,
                svn_revnum_t src_revision,
                const char *dst_relpath,
                apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_copy)(editor->baton, src_relpath, src_revision,
                                  dst_relpath, scratch_pool);
}


svn_error_t *
svn_editor_move(svn_editor_t *editor,
                const char *src_relpath,
                svn_revnum_t src_revision,
                const char *dst_relpath,
                apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_move)(editor->baton, src_relpath, src_revision,
                                  dst_relpath, scratch_pool);
}


svn_error_t *
svn_editor_complete(svn_editor_t *editor,
                    apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_complete)(editor->baton, scratch_pool);
}


svn_error_t *
svn_editor_abort(svn_editor_t *editor,
                 apr_pool_t *scratch_pool)
{
  return (*editor->funcs.cb_abort)(editor->baton, scratch_pool);
}
