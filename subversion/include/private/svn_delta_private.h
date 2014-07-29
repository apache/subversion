/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_delta_private.h
 * @brief The Subversion delta/diff/editor library - Internal routines
 */

#ifndef SVN_DELTA_PRIVATE_H
#define SVN_DELTA_PRIVATE_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_editor.h"
#include "svn_editor3.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


typedef svn_error_t *(*svn_delta__start_edit_func_t)(
  void *baton,
  svn_revnum_t base_revision);

typedef svn_error_t *(*svn_delta__target_revision_func_t)(
  void *baton,
  svn_revnum_t target_revision,
  apr_pool_t *scratch_pool);

typedef svn_error_t *(*svn_delta__unlock_func_t)(
  void *baton,
  const char *path,
  apr_pool_t *scratch_pool);


/* See svn_editor__insert_shims() for more information. */
struct svn_delta__extra_baton
{
  svn_delta__start_edit_func_t start_edit;
  svn_delta__target_revision_func_t target_revision;
  void *baton;
};


/** A temporary API to convert from a delta editor to an Ev2 editor. */
svn_error_t *
svn_delta__editor_from_delta(svn_editor_t **editor_p,
                             struct svn_delta__extra_baton **exb,
                             svn_delta__unlock_func_t *unlock_func,
                             void **unlock_baton,
                             const svn_delta_editor_t *deditor,
                             void *dedit_baton,
                             svn_boolean_t *send_abs_paths,
                             const char *repos_root,
                             const char *base_relpath,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             svn_delta_fetch_kind_func_t fetch_kind_func,
                             void *fetch_kind_baton,
                             svn_delta_fetch_props_func_t fetch_props_func,
                             void *fetch_props_baton,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);


/** A temporary API to convert from an Ev2 editor to a delta editor. */
svn_error_t *
svn_delta__delta_from_editor(const svn_delta_editor_t **deditor,
                             void **dedit_baton,
                             svn_editor_t *editor,
                             svn_delta__unlock_func_t unlock_func,
                             void *unlock_baton,
                             svn_boolean_t *found_abs_paths,
                             const char *repos_root,
                             const char *base_relpath,
                             svn_delta_fetch_props_func_t fetch_props_func,
                             void *fetch_props_baton,
                             svn_delta_fetch_base_func_t fetch_base_func,
                             void *fetch_base_baton,
                             struct svn_delta__extra_baton *exb,
                             apr_pool_t *pool);

/* An object for communicating out-of-band details between an Ev1-to-Ev3
 * shim and an Ev3-to-Ev1 shim. */
typedef struct svn_delta__shim_connector_t svn_delta__shim_connector_t;

/* Return an Ev3 editor in *EDITOR_P which will drive the Ev1 delta
 * editor DEDITOR/DEDIT_BATON.
 *
 * This editor buffers all the changes and then drives the Ev1 when the
 * returned editor's "close" method is called.
 *
 * This editor converts moves into copy-and-delete. It presently makes a
 * one-way (lossy) conversion.
 *
 *   TODO: Option to pass the 'move' information through as some sort of
 *   metadata so that it can be preserved in an Ev3-Ev1-Ev3 round-trip
 *   conversion.
 *     - Use 'entry-props'?
 *     - Send copy-and-delete with copy-from-rev = -1?
 *
 * This editor implements the "incremental changes" variant of the Ev3
 * commit editor interface.
 *
 * SHIM_CONNECTOR can be used to enable a more exact round-trip conversion
 * from an Ev1 drive to Ev3 and back to Ev1. The caller should pass the
 * returned *SHIM_CONNECTOR value to svn_delta__delta_from_ev3_for_commit().
 * SHIM_CONNECTOR may be null if not wanted.
 *
 * REPOS_ROOT_URL is the repository root URL, and BASE_RELPATH is the
 * relative path within the repository of the root directory of the edit.
 * (An Ev1 edit must be rooted at a directory, not at a file.)
 *
 * FETCH_KIND_FUNC / FETCH_KIND_BATON: A callback by which the shim may
 * determine the kind of a path. This is called for a copy source or move
 * source node, passing the Ev3 relpath and the specific copy-from
 * revision.
 *
 * FETCH_PROPS_FUNC / FETCH_PROPS_BATON: A callback by which the shim may
 * determine the existing properties on a path. This is called for a copy
 * source or move source node or a modified node, but not for a simple
 * add, passing the Ev3 relpath and the specific revision.
 *
 * CANCEL_FUNC / CANCEL_BATON: The usual cancellation callback; folded
 * into the produced editor. May be NULL/NULL if not wanted.
 *
 * Allocate the new editor in RESULT_POOL, which may become large and must
 * live for the lifetime of the edit. Use SCRATCH_POOL for temporary
 * allocations.
 */
svn_error_t *
svn_delta__ev3_from_delta_for_commit(
                        svn_editor3_t **editor_p,
                        svn_delta__shim_connector_t **shim_connector,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        const char *repos_root,
                        const char *base_relpath,
                        svn_delta_fetch_kind_func_t fetch_kind_func,
                        void *fetch_kind_baton,
                        svn_delta_fetch_props_func_t fetch_props_func,
                        void *fetch_props_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Return a delta editor in DEDITOR/DEDITOR_BATON which will drive EDITOR.
 *
 * REPOS_ROOT_URL is the repository root URL, and BASE_RELPATH is the
 * relative path within the repository of the root directory of the edit.
 * (An Ev1 edit must be rooted at a directory, not at a file.)
 *
 * FETCH_PROPS_FUNC / FETCH_PROPS_BATON: A callback / baton pair which
 * will be used by the shim handlers if they need to determine the
 * existing properties on a  path.
 *
 * FETCH_BASE_FUNC / FETCH_BASE_BATON: A callback / baton pair which will
 * be used by the shims handlers if they need to determine the base
 * text of a path.  It should only be invoked for files.
 *
 * SHIM_CONNECTOR can be used to enable a more exact round-trip conversion
 * from an Ev1 drive to Ev3 and back to Ev1. It must live for the lifetime
 * of the edit. It may be null if not wanted.
 *
 * Allocate the new editor in RESULT_POOL, which may become large and must
 * live for the lifetime of the edit. Use SCRATCH_POOL for temporary
 * allocations.
 */
svn_error_t *
svn_delta__delta_from_ev3_for_commit(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_editor3_t *editor,
                        const char *repos_root,
                        const char *base_relpath,
                        svn_delta_fetch_props_func_t fetch_props_func,
                        void *fetch_props_baton,
                        svn_delta_fetch_base_func_t fetch_base_func,
                        void *fetch_base_baton,
                        const svn_delta__shim_connector_t *shim_connector,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Return in NEW_DEDITOR/NEW_DETIT_BATON a delta editor that wraps
 * OLD_DEDITOR/OLD_DEDIT_BATON, inserting a pair of shims that convert
 * Ev1 to Ev3 and back to Ev1.
 *
 * REPOS_ROOT_URL is the repository root URL, and BASE_RELPATH is the
 * relative path within the repository of the root directory of the edit.
 *
 * SHIM_CB provides callbacks that the shims may use to fetch details of
 * the base state when needed.
 */
svn_error_t *
svn_editor3__insert_shims(
                        const svn_delta_editor_t **new_deditor,
                        void **new_dedit_baton,
                        const svn_delta_editor_t *old_deditor,
                        void *old_dedit_baton,
                        const char *repos_root,
                        const char *base_relpath,
                        const svn_delta_shim_callbacks_t *shim_cb,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/** Read the txdelta window header from @a stream and return the total
    length of the unparsed window data in @a *window_len. */
svn_error_t *
svn_txdelta__read_raw_window_len(apr_size_t *window_len,
                                 svn_stream_t *stream,
                                 apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DELTA_PRIVATE_H */
