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
 * @file svn_editor3p.h
 * @brief Tree editing
 *
 * @since New in 1.10.
 */

#ifndef SVN_EDITOR3P_H
#define SVN_EDITOR3P_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_io.h"    /* for svn_stream_t  */
#include "svn_delta.h"

#include "private/svn_editor3e.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*
 * TODO:
 *
 *   - Consider edits rooted at a sub-path of the repository. At present,
 *     the editor is designed to be rooted at the repository root.
 */

/**
 * @defgroup svn_editor The editor interface
 * @{
 */

/** Tree Editor
 */
typedef struct svn_editor3p_t svn_editor3p_t;


/** These functions are called by the tree delta driver to edit the target.
 *
 * @see #svn_editor3p_t
 *
 * @defgroup svn_editor3p_drive Driving the editor
 * @{
 */

/*
 * ===================================================================
 * Editor for Commit (incremental tree changes; path-based addressing)
 * ===================================================================
 *
 * Edit Operations:
 *
 *   - mk   kind                dir-location[1]  new-name[2]
 *   - cp   ^/from-path@rev[3]  dir-location[1]  new-name[2]
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 *   - cp   from-path[4]        dir-location[1]  new-name[2]
 * </SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 *   - mv   location[1]         dir-location[1]  new-name[2]
 *   - res  ^/from-path@rev[3]  dir-location[1]  new-name[2]
 *   - rm                       pegged-path[1]
 *   - put  new-content         pegged-path[1]
 *
 *   [*] 'location' means the tuple (^/peg-path @ peg-rev, created-relpath)
 *
 * Preconditions:
 *
 *   [1] this node-branch must exist in txn
 *   [2] a child with this name must not exist in the parent dir in txn
 *       (as far as sender knows; the rebase will check whether it
 *        exists and/or can be merged on receiver side)
 *   [3] this node-rev must exist in committed revision
 *   [4] this path must exist in txn
 *
 * Characteristics of this editor:
 *
 *   - Tree changes are ordered.
 * 
 *   - Content changes are unordered and independent.
 *
 *     Each node's content is set or altered at most once, and only for
 *     nodes present in the final state.
 *
 *   - There can be more than one move operation per node. Some changes
 *     require a node to be moved to a temporary location and then moved
 *     again to its final location. This could be restricted to at most
 *     two moves per node. Temporary move(s) could be required to use a
 *     defined temporary name space.
 * 
 *     There is not (yet) a defined canonical sequence of editor operations
 *     to represent an arbitrary change.
 *
 *   - The sender needs a name space it can use for temporary paths.
 *
 *     If the receiver will be applying changes to a state that may not
 *     exactly match the sender's base state, such as a commit editor,
 *     it is necessary that the temporary paths will not clash with other
 *     paths present on the receiving side. It may also be useful for the
 *     receiver to be aware of the temporary name space so that it can
 *     optimise temporary moves differently from other moves.
 *
 *   - All tree changes MAY be sent before all content changes.
 *
 *   - Copying or deleting a subtree is an O(1) cheap operation.
 *
 *   - The commit rebase MAY (but need not) merge a repository-side move
 *     with incoming edits inside the moved subtree, and vice-versa.
 *
 *   ### In order to expand the scope of this editor to situations like
 *       update/switch, where the receiver doesn't have the repository
 *       to refer to, Can we add a full-traversal kind of copy?
 *       Is that merely a matter of driving the same API in a different
 *       way ("let the copy operation mean non-recursive copy")? Or is
 *       it totally out of scope? (To support WC update we need other
 *       changes too, not just this.)
 *
 * Notes on Paths:
 *
 *   - Each node in the txn was either pre-existing or was created within
 *     the txn. A pre-existing node may be moved by the rebase-on-commit
 *     and/or by operations within the txn, whereas a created node is
 *     required to remain at the same path where it was created, relative
 *     to its pathwise-nearest pre-existing node.
 *
 *     We refer to a node in a txn by means of a pegged path and a created
 *     relative path:
 *
 *       (^/peg-path @ peg-rev, created-relpath).
 *
 *     The "path @ rev" part identifies the nearest pre-existing node-branch,
 *     by reference to a path in a committed revision which is to be
 *     traced forward to the current transaction. The Out-Of-Date
 *     check notes whether the specified node-branch still exists in
 *     the txn, and, if applicable, that it hasn't been modified.
 *
 *     Each component of the "created-relpath" refers to a node that was
 *     created within the txn (with "mk" or "cp", but not "res"). It MUST
 *     NOT refer to a node-branch that already existed before the edit
 *     began. The "created-relpath" may be empty.
 *
 *   - Ev1 referred to each node in a txn by a nesting of "open" (for a
 *     pre-existing node) and "add" (for a created node) operations.
 *
 * Notes on Copying:
 *
 *   - Copy from path-in-txn is required iff we want to support copying
 *     from "this revision". If we don't then the source is necessarily
 *     a pre-existing node and so can be referenced by ^/path@rev.
 *
 *   - There is no provision for making a non-tracked copy of a subtree
 *     in a single operation.
 *
 * Notes on Moving:
 *
 *   - There is no operation to move a subtree whose root node was created
 *     in this txn, merely because it is not necessary. (A node created by
 *     "mk" can always be created in the required location. A subtree of a
 *     copy can be moved by deleting it and making a new copy from the
 *     corresponding subtree of the original copy root, as there is no
 *     distinction between the first copy and the second copy.)
 *
 */

/** Make a single new node ("versioned object") with empty content.
 * 
 * Set the node kind to @a new_kind. Create the node in the parent
 * directory node-branch specified by @a parent_loc. Set the new node's
 * name to @a new_name.
 *
 * The new node is not related by node identity to any other existing node
 * nor to any other node created by another "mk" operation.
 *
 * @node "put" is optional for a node made by "mk".
 * ### For use as an 'update' editor, maybe 'mk' without 'put' should
 *     make an 'absent' node.
 *
 * @see #svn_editor3p_t
 */
svn_error_t *
svn_editor3p_mk(svn_editor3p_t *editor,
                svn_node_kind_t new_kind,
                svn_editor3_txn_path_t parent_loc,
                const char *new_name);

/** Create a copy of a subtree.
 * 
 * The source subtree is found at @a from_loc. Create the root node of
 * the new subtree in the parent directory node-branch specified by
 * @a parent_loc with the name @a new_name.
 *
 * Each node in the target subtree has a "copied from" relationship with
 * the node with the corresponding path in the source subtree.
 *
 * <SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 * If @a from_loc has a non-empty "created relpath", then it refers to the
 * current state in the txn.
 *   ### Or use some other indication, such as (from_loc.rev == -1)?
 * Make a copy of the current state of that subtree in the txn. When
 * committed, the copy will have a "copied from" reference to the
 * committed revision.
 *
 * Modifying the source subtree later within this edit will not affect
 * the target's tree structure and content, but will modify the copy
 * relationships of the target subtree accordingly. Moving a source
 * node (directly or as a child) will update the corresponding target's
 * "copied from" reference to follow it.
 *   ### Except if we move a source node into the target subtree, ...?
 * Deleting a source node will
 * remove the corresponding target node's "copied from" reference.
 * </SVN_EDITOR3_WITH_COPY_FROM_THIS_REV>
 *
 * The content of each node in the target subtree is by default the
 * content of the node at the corresponding path within the source
 * subtree, and MAY be changed by a "put" operation.
 *
 * @see #svn_editor3p_t
 */
svn_error_t *
svn_editor3p_cp(svn_editor3p_t *editor,
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
                svn_editor3p_txn_path_t from_loc,
#else
                svn_pathrev_t from_loc,
#endif
                svn_editor3_txn_path_t parent_loc,
                const char *new_name);

/** Move a subtree to a new parent directory and/or a new name.
 *
 * The root node of the source subtree is specified by @a from_loc
 * which refers to a committed revision. This node must exist in the
 * current txn, but may have been moved and/or modified. (This method
 * cannot be used to move a node that has been created within the edit.)
 *
 * Move the root node of the subtree to the parent directory node-branch
 * specified by @a new_parent_loc and change its name to @a new_name.
 *
 * Each node in the target subtree remains the same node-branch as
 * the node with the corresponding path in the source subtree.
 *
 * Any modifications that have already been made within the subtree are
 * preserved.
 *
 * @see #svn_editor3p_t
 */
svn_error_t *
svn_editor3p_mv(svn_editor3p_t *editor,
                svn_pathrev_t from_loc,
                svn_editor3_txn_path_t new_parent_loc,
                const char *new_name);

#ifdef SVN_EDITOR3_WITH_RESURRECTION
/** Resurrect a previously deleted node-branch.
 *
 * Resurrect the node-branch that previously existed at @a from_loc,
 * a location in a committed revision. Put the resurrected node at
 * @a parent_loc, @a new_name.
 *
 * The content of the resurrected node is, by default, the content of the
 * source node at @a from_loc. The content MAY be changed by a "put".
 *
 * The specified source is any location at which this node-branch existed,
 * not necessarily at its youngest revision nor even within its most
 * recent period of existence.
 *
 * ### The source node-branch MUST NOT exist in the txn. If the source
 *     node-branch exists in the txn-base, resurrection would be
 *     equivalent to reverting a local delete in the txn; the sender
 *     SHOULD NOT do this. [### Why not? Just because it seems like
 *     unnecessary flexibility.]
 *
 * ### Can we have a recursive resurrect operation? What should it do
 *     if a child node is still alive (moved or already resurrected)?
 *
 * @see #svn_editor3p_t
 */
svn_error_t *
svn_editor3p_res(svn_editor3p_t *editor,
                 svn_pathrev_t from_loc,
                 svn_editor3_txn_path_t parent_loc,
                 const char *new_name);
#endif

/** Remove the existing node-branch identified by @a loc and, recursively,
 * all nodes that are currently its children in the txn.
 *
 * @note This does not delete nodes that used to be children of the specified
 * node-branch that have since been moved away.
 *
 * @note Each node-branch to be removed, that is each node-branch currently
 * at or below @a loc, MAY be a child of a copy but otherwise SHOULD NOT
 * have been created or modified in this edit. Other node-branches MAY have
 * previously existed under @a loc and been deleted or moved away.
 *
 * @see #svn_editor3p_t
 */
svn_error_t *
svn_editor3p_rm(svn_editor3p_t *editor,
                svn_editor3_txn_path_t loc);

/** Set the content of the node-branch identified by @a loc.
 *
 * Set the content to @a new_content. (The new content may be described
 * in terms of a delta against another node's content.)
 *
 * The caller owns @a new_content, including any file therein, and may
 * destroy it after this call returns.
 *
 * @note "put" MAY be sent for any node that exists in the final state.
 * "put" SHOULD NOT be sent for a node that will not exist in the final
 * state. "put" SHOULD NOT be sent more than once for any node-branch.
 * "put" MUST provide the right kind of content to match the node kind; it
 * cannot change the kind of a node nor convert the content to match the
 * node kind.
 *
 * @see #svn_editor3p_t
 */
svn_error_t *
svn_editor3p_put(svn_editor3p_t *editor,
                 svn_editor3_txn_path_t loc,
                 const svn_element_content_t *new_content);


/** Drive @a editor's #svn_editor3p_cb_complete_t callback.
 *
 * Send word that the edit has been completed successfully.
 *
 * @see #svn_editor3p_t
 */
svn_error_t *
svn_editor3p_complete(svn_editor3p_t *editor);

/** Drive @a editor's #svn_editor3p_cb_abort_t callback.
 *
 * Notify that the edit transmission was not successful.
 * ### TODO @todo Shouldn't we add a reason-for-aborting argument?
 *
 * @see #svn_editor3p_t
 */
svn_error_t *
svn_editor3p_abort(svn_editor3p_t *editor);

/** @} */


/** These function types define the callback functions a tree delta consumer
 * implements.
 *
 * Each of these "receiving" function types matches a "driving" function,
 * which has the same arguments with these differences:
 *
 * - These "receiving" functions have a @a baton argument, which is the
 *   @a editor_baton originally passed to svn_editor3p_create(), as well as
 *   a @a scratch_pool argument.
 *
 * - The "driving" functions have an #svn_editor3p_t* argument, in order to
 *   call the implementations of the function types defined here that are
 *   registered with the given #svn_editor3p_t instance.
 *
 * Note that any remaining arguments for these function types are explained
 * in the comment for the "driving" functions. Each function type links to
 * its corresponding "driver".
 *
 * @see #svn_editor3p_cb_funcs_t, #svn_editor3p_t
 *
 * @defgroup svn_editor_callbacks Editor callback definitions
 * @{
 */

/** @see svn_editor3p_mk(), #svn_editor3p_t
 */
typedef svn_error_t *(*svn_editor3p_cb_mk_t)(
  void *baton,
  svn_node_kind_t new_kind,
  svn_editor3_txn_path_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3p_cp(), #svn_editor3p_t
 */
typedef svn_error_t *(*svn_editor3p_cb_cp_t)(
  void *baton,
#ifdef SVN_EDITOR3_WITH_COPY_FROM_THIS_REV
  svn_editor3_txn_path_t from_loc,
#else
  svn_pathrev_t from_loc,
#endif
  svn_editor3_txn_path_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

/** @see svn_editor3p_mv(), #svn_editor3p_t
 */
typedef svn_error_t *(*svn_editor3p_cb_mv_t)(
  void *baton,
  svn_pathrev_t from_loc,
  svn_editor3_txn_path_t new_parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);

#ifdef SVN_EDITOR3_WITH_RESURRECTION
/** @see svn_editor3p_res(), #svn_editor3p_t
 */
typedef svn_error_t *(*svn_editor3p_cb_res_t)(
  void *baton,
  svn_pathrev_t from_loc,
  svn_editor3_txn_path_t parent_loc,
  const char *new_name,
  apr_pool_t *scratch_pool);
#endif

/** @see svn_editor3p_rm(), #svn_editor3p_t
 */
typedef svn_error_t *(*svn_editor3p_cb_rm_t)(
  void *baton,
  svn_editor3_txn_path_t loc,
  apr_pool_t *scratch_pool);

/** @see svn_editor3p_put(), #svn_editor3p_t
 */
typedef svn_error_t *(*svn_editor3p_cb_put_t)(
  void *baton,
  svn_editor3_txn_path_t loc,
  const svn_element_content_t *new_content,
  apr_pool_t *scratch_pool);

/** @see svn_editor3p_complete(), #svn_editor3p_t
 */
typedef svn_error_t *(*svn_editor3p_cb_complete_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/** @see svn_editor3p_abort(), #svn_editor3p_t
 */
typedef svn_error_t *(*svn_editor3p_cb_abort_t)(
  void *baton,
  apr_pool_t *scratch_pool);

/** @} */


/** These functions create an editor instance so that it can be driven.
 *
 * @defgroup svn_editor3p_create Editor creation
 * @{
 */

/** A set of editor callback functions.
 *
 * If a function pointer is NULL, it will not be called.
 *
 * @see svn_editor3p_create(), #svn_editor3p_t
 */
typedef struct svn_editor3p_cb_funcs_t
{
  svn_editor3p_cb_mk_t cb_mk;
  svn_editor3p_cb_cp_t cb_cp;
  svn_editor3p_cb_mv_t cb_mv;
#ifdef SVN_EDITOR3_WITH_RESURRECTION
  svn_editor3p_cb_res_t cb_res;
#endif
  svn_editor3p_cb_rm_t cb_rm;
  svn_editor3p_cb_put_t cb_put;

  svn_editor3p_cb_complete_t cb_complete;
  svn_editor3p_cb_abort_t cb_abort;

} svn_editor3p_cb_funcs_t;

/** Allocate an #svn_editor3p_t instance from @a result_pool, store
 * @a *editor_funcs, @a editor_baton, @a cancel_func and @a cancel_baton
 * in the new instance and return it.
 *
 * @a cancel_func / @a cancel_baton may be NULL / NULL if not wanted.
 *
 * @see #svn_editor3p_t
 */
svn_editor3p_t *
svn_editor3p_create(const svn_editor3p_cb_funcs_t *editor_funcs,
                    void *editor_baton,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *result_pool);

/** Return an editor's private baton.
 *
 * In some cases, the baton is required outside of the callbacks. This
 * function returns the private baton for use.
 *
 * @note Not a good public API, as outside the callbacks one generally
 * doesn't know whether the editor given is the interesting editor or a
 * wrapper around it.
 *
 * @see svn_editor3p_create(), #svn_editor3p_t
 */
void *
svn_editor3p__get_baton(const svn_editor3p_t *editor);

/** @} */

/** @} */


/* ====================================================================== */

#ifdef SVN_DEBUG
/** Return an editor in @a *editor_p which will forward all calls to the
 * @a wrapped_editor while printing a diagnostic trace of the calls to
 * standard output, prefixed with 'DBG:'.
 *
 * The wrapper editor will not perform cancellation checking.
 *
 * Allocate *editor_p in RESULT_POOL.
 */
svn_error_t *
svn_editor3p__get_debug_editor(svn_editor3p_t **editor_p,
                               svn_editor3p_t *wrapped_editor,
                               apr_pool_t *result_pool);
#endif


/* An object for communicating out-of-band details between an Ev1-to-Ev3
 * shim and an Ev3-to-Ev1 shim. */
typedef struct svn_editor3p__shim_connector_t svn_editor3p__shim_connector_t;

/* Like svn_delta__ev3_from_delta_for_commit2(), except:
 *   - doesn't take the 'branching_txn' parameter;
 *   - implements the "incremental changes" variant of the Ev3
 *     commit editor interface.
 */
svn_error_t *
svn_delta__ev3_from_delta_for_commit(
                        svn_editor3p_t **editor_p,
                        svn_editor3p__shim_connector_t **shim_connector,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        const char *repos_root_url,
                        const char *base_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
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
 * FETCH_FUNC/FETCH_BATON is a callback by which the shim may retrieve the
 * original or copy-from kind/properties/text for a path being committed.
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
                        svn_editor3p_t *editor,
                        const char *repos_root_url,
                        const char *base_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        const svn_editor3p__shim_connector_t *shim_connector,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Return in NEW_DEDITOR/NEW_DETIT_BATON a delta editor that wraps
 * OLD_DEDITOR/OLD_DEDIT_BATON, inserting a pair of shims that convert
 * Ev1 to Ev3 and back to Ev1.
 *
 * REPOS_ROOT_URL is the repository root URL, and BASE_RELPATH is the
 * relative path within the repository of the root directory of the edit.
 *
 * FETCH_FUNC/FETCH_BATON is a callback by which the shim may retrieve the
 * original or copy-from kind/properties/text for a path being committed.
 */
svn_error_t *
svn_editor3p__insert_shims(
                        const svn_delta_editor_t **new_deditor,
                        void **new_dedit_baton,
                        const svn_delta_editor_t *old_deditor,
                        void *old_dedit_baton,
                        const char *repos_root,
                        const char *base_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* A callback for declaring the target revision of an update or switch.
 */
typedef svn_error_t *(*svn_editor3__set_target_revision_func_t)(
  void *baton,
  svn_revnum_t target_revision,
  apr_pool_t *scratch_pool);

/* An update (or switch) editor.
 *
 * This consists of a plain Ev3 editor and the additional methods or
 * resources needed for use as an update or switch editor.
 */
typedef struct svn_update_editor3_t {
  /* The basic editor. */
  svn_editor3p_t *editor;

  /* A method to communicate the target revision of the update (or switch),
   * to be called before driving the editor. It has its own baton, rather
   * than using the editor's baton, so that the editor can be replaced (by
   * a wrapper editor, typically) without having to wrap this callback. */
  svn_editor3__set_target_revision_func_t set_target_revision_func;
  void *set_target_revision_baton;
} svn_update_editor3_t;

/* Like svn_delta__ev3_from_delta_for_commit() but for an update editor.
 */
svn_error_t *
svn_delta__ev3_from_delta_for_update(
                        svn_update_editor3_t **editor_p,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Like svn_delta__delta_from_ev3_for_commit() but for an update editor.
 */
svn_error_t *
svn_delta__delta_from_ev3_for_update(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_update_editor3_t *update_editor,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_editor3__shim_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_EDITOR3P_H */
