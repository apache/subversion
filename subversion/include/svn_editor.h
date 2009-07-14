/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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
 * @endcopyright
 *
 * @file svn_editor.h
 * @brief Tree editing functions and structures
 */

#ifndef SVN_EDITOR_H
#define SVN_EDITOR_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"    /* for svn_stream_t  */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** An abstract object that edits a target tree.
 *
 * ### more docco
 */
typedef struct svn_editor_t svn_editor_t;


typedef svn_error_t *(*svn_editor_cb_add_directory_t)(
  void *baton,
  const char *relpath,
  const apr_array_header_t *children,
  apr_hash_t *props,
  svn_revnum_t replaces_rev,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_add_file_t)(
  void *baton,
  const char *relpath,
  apr_hash_t *props,
  svn_revnum_t replaces_rev,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_add_symlink_t)(
  void *baton,
  const char *relpath,
  const char *target,
  apr_hash_t *props,
  svn_revnum_t replaces_rev,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_add_absent_t)(
  void *baton,
  const char *relpath,
  svn_node_kind_t kind,
  svn_revnum_t replaces_rev,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_set_props_t)(
  void *baton,
  const char *relpath,
  svn_revnum_t revision,
  apr_hash_t *props,
  svn_boolean_t complete,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_set_text_t)(
  void *baton,
  const char *relpath,
  svn_revnum_t revision,
  const svn_checksum_t *checksum,
  svn_stream_t *contents,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_set_target_t)(
  void *baton,
  const char *relpath,
  svn_revnum_t revision,
  const char *target,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_delete_t)(
  void *baton,
  const char *relpath,
  svn_revnum_t revision,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_copy_t)(
  void *baton,
  const char *src_relpath,
  svn_revnum_t src_revision,
  const char *dst_relpath,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_move_t)(
  void *baton,
  const char *src_relpath,
  svn_revnum_t src_revision,
  const char *dst_relpath,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_complete_t)(
  void *baton,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_editor_cb_abort_t)(
  void *baton,
  apr_pool_t *scratch_pool);


svn_error_t *
svn_editor_create(svn_editor_t **editor,
                  void *editor_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool);


svn_error_t *
svn_editor_setcb_add_directory(svn_editor_t *editor,
                               svn_editor_cb_add_directory_t callback,
                               apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_add_file(svn_editor_t *editor,
                          svn_editor_cb_add_file_t callback,
                          apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_add_symlink(svn_editor_t *editor,
                             svn_editor_cb_add_symlink_t callback,
                             apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_add_absent(svn_editor_t *editor,
                            svn_editor_cb_add_absent_t callback,
                            apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_set_props(svn_editor_t *editor,
                           svn_editor_cb_set_props_t callback,
                           apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_set_text(svn_editor_t *editor,
                          svn_editor_cb_set_text_t callback,
                          apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_set_target(svn_editor_t *editor,
                            svn_editor_cb_set_target_t callback,
                            apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_delete(svn_editor_t *editor,
                        svn_editor_cb_delete_t callback,
                        apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_copy(svn_editor_t *editor,
                      svn_editor_cb_copy_t callback,
                      apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_move(svn_editor_t *editor,
                      svn_editor_cb_move_t callback,
                      apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_complete(svn_editor_t *editor,
                          svn_editor_cb_complete_t callback,
                          apr_pool_t *scratch_pool);

svn_error_t *
svn_editor_setcb_abort(svn_editor_t *editor,
                       svn_editor_cb_abort_t callback,
                       apr_pool_t *scratch_pool);


typedef struct
{
  svn_editor_cb_add_directory_t cb_add_directory;
  svn_editor_cb_add_file_t cb_add_file;
  svn_editor_cb_add_symlink_t cb_add_symlink;
  svn_editor_cb_add_absent_t cb_add_absent;
  svn_editor_cb_set_props_t cb_set_props;
  svn_editor_cb_set_text_t cb_set_text;
  svn_editor_cb_set_target_t cb_set_target;
  svn_editor_cb_delete_t cb_delete;
  svn_editor_cb_copy_t cb_copy;
  svn_editor_cb_move_t cb_move;
  svn_editor_cb_complete_t cb_complete;
  svn_editor_cb_abort_t cb_abort;

} svn_editor_cb_many_t;

svn_error_t *
svn_editor_setcb_many(svn_editor_t *editor,
                      const svn_editor_cb_many_t *many,
                      apr_pool_t *scratch_pool);


/* ### driving the editor  */

svn_error_t *
svn_editor_add_directory(svn_editor_t *editor,
                         const char *relpath,
                         const apr_array_header_t *children,
                         apr_hash_t *props,
                         svn_revnum_t replaces_rev);

svn_error_t *
svn_editor_add_file(svn_editor_t *editor,
                    const char *relpath,
                    apr_hash_t *props,
                    svn_revnum_t replaces_rev);

svn_error_t *
svn_editor_add_symlink(svn_editor_t *editor,
                       const char *relpath,
                       const char *target,
                       apr_hash_t *props,
                       svn_revnum_t replaces_rev);

svn_error_t *
svn_editor_add_absent(svn_editor_t *editor,
                      const char *relpath,
                      svn_node_kind_t kind,
                      svn_revnum_t replaces_rev);

svn_error_t *
svn_editor_set_props(svn_editor_t *editor,
                     const char *relpath,
                     svn_revnum_t revision,
                     apr_hash_t *props,
                     svn_boolean_t complete);

svn_error_t *
svn_editor_set_text(svn_editor_t *editor,
                    const char *relpath,
                    svn_revnum_t revision,
                    const svn_checksum_t *checksum,
                    svn_stream_t *contents);

svn_error_t *
svn_editor_set_target(svn_editor_t *editor,
                      const char *relpath,
                      svn_revnum_t revision,
                      const char *target);

svn_error_t *
svn_editor_delete(svn_editor_t *editor,
                  const char *relpath,
                  svn_revnum_t revision);

svn_error_t *
svn_editor_copy(svn_editor_t *editor,
                const char *src_relpath,
                svn_revnum_t src_revision,
                const char *dst_relpath);

svn_error_t *
svn_editor_move(svn_editor_t *editor,
                const char *src_relpath,
                svn_revnum_t src_revision,
                const char *dst_relpath);

svn_error_t *
svn_editor_complete(svn_editor_t *editor);

svn_error_t *
svn_editor_abort(svn_editor_t *editor);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_EDITOR_H */
