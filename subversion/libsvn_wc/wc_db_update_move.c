/*
 * wc_db_update_move.c :  updating moves during tree-conflict resolution
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

/* This file implements an editor and an edit driver which are used
 * to resolve an "incoming edit, local move-away" tree conflict resulting
 * from an update (or switch).
 *
 * Our goal is to be able to resolve this conflict such that the end
 * result is just the same as if the user had run the update *before*
 * the local move.
 *
 * When an update (or switch) produces incoming changes for a locally
 * moved-away subtree, it updates the base nodes of the moved-away tree
 * and flags a tree-conflict on the moved-away root node.
 * This editor transfers these changes from the moved-away part of the
 * working copy to the corresponding moved-here part of the working copy.
 *
 * Both the driver and receiver components of the editor are implemented
 * in this file.
 *
 * The driver sees two NODES trees: the move source tree and the move
 * destination tree.  When the move is initially made these trees are
 * equivalent, the destination is a copy of the source.  The source is
 * a single-op-depth, single-revision, deleted layer [1] and the
 * destination has an equivalent single-op-depth, single-revision
 * layer. The destination may have additional higher op-depths
 * representing adds, deletes, moves within the move destination. [2]
 *
 * After the intial move an update has modified the NODES in the move
 * source and may have introduced a tree-conflict since the source and
 * destination trees are no longer equivalent.  The source is a
 * different revision and may have text, property and tree changes
 * compared to the destination.  The driver will compare the two NODES
 * trees and drive an editor to change the destination tree so that it
 * once again matches the source tree.  Changes made to the
 * destination NODES tree to achieve this match will be merged into
 * the working files/directories.
 *
 * The whole drive occurs as one single wc.db transaction.  At the end
 * of the transaction the destination NODES table should have a layer
 * that is equivalent to the source NODES layer, there should be
 * workqueue items to make any required changes to working
 * files/directories in the move destination, and there should be
 * tree-conflicts in the move destination where it was not possible to
 * update the working files/directories.
 *
 * [1] The move source tree is single-revision because we currently do
 *     not allow a mixed-rev move, and therefore it is single op-depth
 *     regardless whether it is a base layer or a nested move.
 *
 * [2] The source tree also may have additional higher op-depths,
 *     representing a replacement, but this editor only reads from the
 *     single-op-depth layer of it, and makes no changes of any kind
 *     within the source tree.
 */

#define SVN_WC__I_AM_WC_DB

#include <assert.h>

#include "svn_checksum.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_props.h"
#include "svn_pools.h"
#include "svn_sorts.h"

#include "private/svn_skel.h"
#include "private/svn_sqlite.h"
#include "private/svn_wc_private.h"
#include "private/svn_editor.h"

#include "wc.h"
#include "props.h"
#include "wc_db_private.h"
#include "wc-queries.h"
#include "conflicts.h"
#include "workqueue.h"
#include "token-map.h"

/*
 * Receiver code.
 *
 * The receiver is an editor that, when driven with a certain change, will
 * merge the edits into the working/actual state of the move destination
 * at MOVE_ROOT_DST_RELPATH (in struct tc_editor_baton), perhaps raising
 * conflicts if necessary.
 *
 * The receiver should not need to refer directly to the move source, as
 * the driver should provide all relevant information about the change to
 * be made at the move destination.
 */

struct tc_editor_baton {
  svn_skel_t **work_items;
  svn_wc__db_t *db;
  svn_wc__db_wcroot_t *wcroot;
  const char *move_root_dst_relpath;

  /* The most recent conflict raised during this drive.  We rely on the
     non-Ev2, depth-first, drive for this to make sense. */
  const char *conflict_root_relpath;

  svn_wc_operation_t operation;
  svn_wc_conflict_version_t *old_version;
  svn_wc_conflict_version_t *new_version;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  apr_pool_t *result_pool;
};

/* 
 * Notifications are delayed until the entire update-move transaction
 * completes. These functions provide the necessary support by storing
 * notification information in a temporary db table (the "update_move_list")
 * and spooling notifications out of that table after the transaction.
 */

/* Add an entry to the notification list. */
static svn_error_t *
update_move_list_add(svn_wc__db_wcroot_t *wcroot,
                     const char *local_relpath,
                     svn_wc_notify_action_t action,
                     svn_node_kind_t kind,
                     svn_wc_notify_state_t content_state,
                     svn_wc_notify_state_t prop_state)

{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_INSERT_UPDATE_MOVE_LIST));
  SVN_ERR(svn_sqlite__bindf(stmt, "sdddd", local_relpath,
                            action, kind, content_state, prop_state));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

/* Send all notifications stored in the notification list, and then
 * remove the temporary database table. */
static svn_error_t *
update_move_list_notify(svn_wc__db_wcroot_t *wcroot,
                        svn_revnum_t old_revision,
                        svn_revnum_t new_revision,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_UPDATE_MOVE_LIST));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  iterpool = svn_pool_create(scratch_pool);
  while (have_row)
    {
      const char *local_relpath;
      svn_wc_notify_action_t action;
      svn_wc_notify_t *notify;

      svn_pool_clear(iterpool);

      local_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      action = svn_sqlite__column_int(stmt, 1);
      notify = svn_wc_create_notify(svn_dirent_join(wcroot->abspath,
                                                    local_relpath,
                                                    iterpool),
                                    action, iterpool);
      notify->kind = svn_sqlite__column_int(stmt, 2);
      notify->content_state = svn_sqlite__column_int(stmt, 3);
      notify->prop_state = svn_sqlite__column_int(stmt, 4);
      notify->old_revision = old_revision;
      notify->revision = new_revision;
      notify_func(notify_baton, notify, scratch_pool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  svn_pool_destroy(iterpool);
  SVN_ERR(svn_sqlite__reset(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_FINALIZE_UPDATE_MOVE));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

static svn_error_t *
mark_tree_conflict(struct tc_editor_baton *b,
                   const char *local_relpath,
                   svn_node_kind_t old_kind,
                   svn_node_kind_t new_kind,
                   svn_wc_conflict_reason_t reason,
                   svn_wc_conflict_action_t action,
                   svn_skel_t *conflict,
                   apr_pool_t *scratch_pool)
{
  const char *repos_relpath;
  svn_wc_conflict_version_t *old_version, *new_version;

  if (!conflict)
    conflict = svn_wc__conflict_skel_create(scratch_pool);

  b->conflict_root_relpath = apr_pstrdup(b->result_pool, local_relpath);

  SVN_ERR(svn_wc__conflict_skel_add_tree_conflict(
                     conflict, NULL,
                     svn_dirent_join(b->wcroot->abspath, local_relpath,
                                     scratch_pool),
                     reason,
                     action,
                     scratch_pool,
                     scratch_pool));

  if (reason != svn_wc_conflict_reason_unversioned)
    {
      repos_relpath = svn_relpath_join(b->old_version->path_in_repos,
                           svn_relpath_skip_ancestor(b->move_root_dst_relpath,
                                                     local_relpath),
                                       scratch_pool);
      old_version = svn_wc_conflict_version_create2(b->old_version->repos_url,
                                                    b->old_version->repos_uuid,
                                                    repos_relpath,
                                                    b->old_version->peg_rev,
                                                    old_kind,
                                                    scratch_pool);
    }
  else
    old_version = NULL;

  repos_relpath = svn_relpath_join(b->new_version->path_in_repos,
                           svn_relpath_skip_ancestor(b->move_root_dst_relpath,
                                                     local_relpath),
                                   scratch_pool);
  new_version = svn_wc_conflict_version_create2(b->new_version->repos_url,
                                                b->new_version->repos_uuid,
                                                repos_relpath,
                                                b->new_version->peg_rev,
                                                new_kind,
                                                scratch_pool);

  if (b->operation == svn_wc_operation_update)
    {
      SVN_ERR(svn_wc__conflict_skel_set_op_update(
                conflict, old_version, new_version,
                scratch_pool, scratch_pool));
    }
  else
    {
      assert(b->operation == svn_wc_operation_switch);
      SVN_ERR(svn_wc__conflict_skel_set_op_switch(
                  conflict, old_version, new_version,
                  scratch_pool, scratch_pool));
    }

  SVN_ERR(svn_wc__db_mark_conflict_internal(b->wcroot, local_relpath,
                                            conflict, scratch_pool));

  return SVN_NO_ERROR;
}

/* If LOCAL_RELPATH is a child of the most recently raised
   tree-conflict or is shadowed then set *IS_CONFLICTED to TRUE and
   raise a tree-conflict on the root of the obstruction if such a
   tree-conflict does not already exist.  KIND is the kind of the
   incoming LOCAL_RELPATH. This relies on the non-Ev2, depth-first,
   drive. */
static svn_error_t *
check_tree_conflict(svn_boolean_t *is_conflicted,
                    struct tc_editor_baton *b,
                    const char *local_relpath,
                    svn_node_kind_t kind,
                    svn_wc_conflict_action_t action,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int dst_op_depth = relpath_depth(b->move_root_dst_relpath);
  int op_depth;
  svn_node_kind_t old_kind;
  const char *conflict_root_relpath = local_relpath;
  const char *moved_to_relpath;
  svn_skel_t *conflict;

  if (b->conflict_root_relpath)
    {
      if (svn_relpath_skip_ancestor(b->conflict_root_relpath, local_relpath))
        {
          *is_conflicted = TRUE;
          return SVN_NO_ERROR;
        }
      b->conflict_root_relpath = NULL;
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                    STMT_SELECT_LOWEST_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", b->wcroot->wc_id, local_relpath,
                            dst_op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      op_depth = svn_sqlite__column_int(stmt, 0);
      old_kind = svn__node_kind_from_kind(svn_sqlite__column_token(stmt, 2,
                                                                   kind_map));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  if (!have_row)
    {
      *is_conflicted = FALSE;
      return SVN_NO_ERROR;
    }

  *is_conflicted = TRUE;

  while (relpath_depth(conflict_root_relpath) > op_depth)
    {
      conflict_root_relpath = svn_relpath_dirname(conflict_root_relpath,
                                                  scratch_pool);
      old_kind = kind = svn_node_dir;
      action = svn_wc_conflict_action_edit;
    }

  SVN_ERR(svn_wc__db_read_conflict_internal(&conflict, b->wcroot,
                                            conflict_root_relpath,
                                            scratch_pool, scratch_pool));

  if (conflict)
    {
      svn_error_t *err
        = svn_wc__conflict_read_tree_conflict(NULL, NULL, 
                                              b->db, b->wcroot->abspath,
                                              conflict,
                                              scratch_pool, scratch_pool);
      if (err && err->apr_err != SVN_ERR_WC_MISSING)
        return err;

      if (!err)
        /* Already a tree-conflict. */
        return SVN_NO_ERROR;

      /* Not a tree-conflict. */
      svn_error_clear(err);
    }

  SVN_ERR(svn_wc__db_scan_deletion_internal(NULL, &moved_to_relpath,
                                            NULL, NULL,
                                            b->wcroot, conflict_root_relpath,
                                            scratch_pool, scratch_pool));

  SVN_ERR(mark_tree_conflict(b, conflict_root_relpath, old_kind, kind,
                             (moved_to_relpath
                              ? svn_wc_conflict_reason_moved_away
                              : svn_wc_conflict_reason_deleted),
                             action, conflict,
                             scratch_pool));
  if (b->notify_func)
    SVN_ERR(update_move_list_add(b->wcroot, local_relpath,
                                 svn_wc_notify_tree_conflict,
                                 kind,
                                 svn_wc_notify_state_inapplicable,
                                 svn_wc_notify_state_inapplicable));

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_add_directory(void *baton,
                        const char *relpath,
                        const apr_array_header_t *children,
                        apr_hash_t *props,
                        svn_revnum_t replaces_rev,
                        apr_pool_t *scratch_pool)
{
  struct tc_editor_baton *b = baton;
  int op_depth = relpath_depth(b->move_root_dst_relpath);
  svn_boolean_t is_conflicted;
  const char *abspath;
  svn_node_kind_t kind;
  svn_skel_t *work_item;
  svn_wc_notify_action_t action = svn_wc_notify_update_add;

  /* Update NODES, only the bits not covered by the later call to
     replace_moved_layer. */
  SVN_ERR(svn_wc__db_extend_parent_delete(b->wcroot, relpath, svn_kind_dir,
                                          op_depth, scratch_pool));

  /* Check for NODES tree-conflict. */
  SVN_ERR(check_tree_conflict(&is_conflicted, b, relpath, svn_node_dir,
                              svn_wc_conflict_action_add,
                              scratch_pool));
  if (is_conflicted)
    return SVN_NO_ERROR;

  /* Check for unversioned tree-conflict */
  abspath = svn_dirent_join(b->wcroot->abspath, relpath, scratch_pool);
  SVN_ERR(svn_io_check_path(abspath, &kind, scratch_pool));

  switch (kind)
    {
    case svn_node_file:
    default:
      SVN_ERR(mark_tree_conflict(b, relpath, kind, svn_node_dir,
                                 svn_wc_conflict_reason_unversioned,
                                 svn_wc_conflict_action_add, NULL,
                                 scratch_pool));
      action = svn_wc_notify_tree_conflict;
      break;

    case svn_node_none:
      SVN_ERR(svn_wc__wq_build_dir_install(&work_item, b->db, abspath,
                                           scratch_pool, b->result_pool));

      SVN_ERR(svn_wc__db_wq_add(b->db, b->wcroot->abspath, work_item,
                                scratch_pool));
      /* Fall through */
    case svn_node_dir:
      break;
    }

  if (b->notify_func)
    SVN_ERR(update_move_list_add(b->wcroot, relpath,
                                 action,
                                 svn_node_dir,
                                 svn_wc_notify_state_inapplicable,
                                 svn_wc_notify_state_inapplicable));
  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_add_file(void *baton,
                   const char *relpath,
                   const svn_checksum_t *checksum,
                   svn_stream_t *contents,
                   apr_hash_t *props,
                   svn_revnum_t replaces_rev,
                   apr_pool_t *scratch_pool)
{
  struct tc_editor_baton *b = baton;
  int op_depth = relpath_depth(b->move_root_dst_relpath);
  svn_boolean_t is_conflicted;
  const char *abspath;
  svn_node_kind_t kind;
  svn_skel_t *work_item;

  /* Update NODES, only the bits not covered by the later call to
     replace_moved_layer. */
  SVN_ERR(svn_wc__db_extend_parent_delete(b->wcroot, relpath, svn_kind_file,
                                          op_depth, scratch_pool));

  /* Check for NODES tree-conflict. */
  SVN_ERR(check_tree_conflict(&is_conflicted, b, relpath, svn_node_file,
                              svn_wc_conflict_action_add,
                              scratch_pool));
  if (is_conflicted)
    return SVN_NO_ERROR;

  /* Check for unversioned tree-conflict */
  abspath = svn_dirent_join(b->wcroot->abspath, relpath, scratch_pool);
  SVN_ERR(svn_io_check_path(abspath, &kind, scratch_pool));

  if (kind != svn_node_none)
    {
      SVN_ERR(mark_tree_conflict(b, relpath, kind, svn_node_file,
                                 svn_wc_conflict_reason_unversioned,
                                 svn_wc_conflict_action_add, NULL,
                                 scratch_pool));
      if (b->notify_func)
        SVN_ERR(update_move_list_add(b->wcroot, relpath,
                                     svn_wc_notify_tree_conflict,
                                     svn_node_file,
                                     svn_wc_notify_state_inapplicable,
                                     svn_wc_notify_state_inapplicable));
      return SVN_NO_ERROR;
    }

  /* Update working file. */
  SVN_ERR(svn_wc__wq_build_file_install(&work_item, b->db,
                                        svn_dirent_join(b->wcroot->abspath,
                                                        relpath,
                                                        scratch_pool),
                                        NULL,
                                        FALSE /* FIXME: use_commit_times? */,
                                        TRUE  /* record_file_info */,
                                        scratch_pool, b->result_pool));

  SVN_ERR(svn_wc__db_wq_add(b->db, b->wcroot->abspath, work_item,
                            scratch_pool));

  if (b->notify_func)
    SVN_ERR(update_move_list_add(b->wcroot, relpath,
                                 svn_wc_notify_update_add,
                                 svn_node_file,
                                 svn_wc_notify_state_inapplicable,
                                 svn_wc_notify_state_inapplicable));
  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_add_symlink(void *baton,
                      const char *relpath,
                      const char *target,
                      apr_hash_t *props,
                      svn_revnum_t replaces_rev,
                      apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
}

static svn_error_t *
tc_editor_add_absent(void *baton,
                     const char *relpath,
                     svn_kind_t kind,
                     svn_revnum_t replaces_rev,
                     apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
}

/* All the info we need about one version of a working node. */
typedef struct working_node_version_t
{
  svn_wc_conflict_version_t *location_and_kind;
  apr_hash_t *props;
  const svn_checksum_t *checksum; /* for files only */
} working_node_version_t;

/* ### ...
 * ### need to pass in the node kinds (before & after)?
 */
static svn_error_t *
create_conflict_markers(svn_skel_t **work_items,
                        const char *local_abspath,
                        svn_wc__db_t *db,
                        const char *repos_relpath,
                        svn_skel_t *conflict_skel,
                        svn_wc_operation_t operation,
                        const working_node_version_t *old_version,
                        const working_node_version_t *new_version,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_skel_t *work_item;
  svn_wc_conflict_version_t *original_version;

  original_version = svn_wc_conflict_version_dup(
                       old_version->location_and_kind, scratch_pool);
  original_version->path_in_repos = repos_relpath;
  original_version->node_kind = svn_node_file;  /* ### ? */
  if (operation == svn_wc_operation_update)
    {
      SVN_ERR(svn_wc__conflict_skel_set_op_update(
                conflict_skel, original_version,
                NULL /* ### derive from new_version & new kind? */,
                scratch_pool, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_wc__conflict_skel_set_op_switch(
                conflict_skel, original_version,
                NULL /* ### derive from new_version & new kind? */,
                scratch_pool, scratch_pool));
    }

  /* According to this func's doc string, it is "Currently only used for
   * property conflicts as text conflict markers are just in-wc files." */
  SVN_ERR(svn_wc__conflict_create_markers(&work_item, db,
                                          local_abspath,
                                          conflict_skel,
                                          result_pool,
                                          scratch_pool));
  *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
update_working_props(svn_wc_notify_state_t *prop_state,
                     svn_skel_t **conflict_skel,
                     apr_array_header_t **propchanges,
                     apr_hash_t **actual_props,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     const struct working_node_version_t *old_version,
                     const struct working_node_version_t *new_version,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  apr_hash_t *new_actual_props;
  apr_array_header_t *new_propchanges;

  /*
   * Run a 3-way prop merge to update the props, using the pre-update
   * props as the merge base, the post-update props as the
   * merge-left version, and the current props of the
   * moved-here working file as the merge-right version.
   */
  SVN_ERR(svn_wc__db_read_props(actual_props,
                                db, local_abspath,
                                result_pool, scratch_pool));
  SVN_ERR(svn_prop_diffs(propchanges, new_version->props, old_version->props,
                         result_pool));
  SVN_ERR(svn_wc__merge_props(conflict_skel, prop_state,
                              &new_actual_props,
                              db, local_abspath,
                              old_version->props, old_version->props,
                              *actual_props, *propchanges,
                              result_pool, scratch_pool));

  /* Setting properties in ACTUAL_NODE with svn_wc__db_op_set_props
     relies on NODES row having been updated first which we don't do
     at present. So this extra property diff has the same effect.

     ### Perhaps we should update NODES first (but after
     ### svn_wc__db_read_props above)?  */
  SVN_ERR(svn_prop_diffs(&new_propchanges, new_actual_props, new_version->props,
                         scratch_pool));
  if (!new_propchanges->nelts)
    new_actual_props = NULL;

  /* Install the new actual props. Don't set the conflict_skel yet, because
     we might need to add a text conflict to it as well. */
  SVN_ERR(svn_wc__db_op_set_props(db, local_abspath,
                                  new_actual_props,
                                  svn_wc__has_magic_property(*propchanges),
                                  NULL/*conflict_skel*/, NULL/*work_items*/,
                                  scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_alter_directory(void *baton,
                          const char *dst_relpath,
                          svn_revnum_t expected_move_dst_revision,
                          const apr_array_header_t *children,
                          apr_hash_t *new_props,
                          apr_pool_t *scratch_pool)
{
  struct tc_editor_baton *b = baton;
  const char *move_dst_repos_relpath;
  svn_revnum_t move_dst_revision;
  svn_kind_t move_dst_kind;
  working_node_version_t old_version, new_version;
  svn_wc__db_status_t status;
  svn_boolean_t is_conflicted;

  SVN_ERR_ASSERT(expected_move_dst_revision == b->old_version->peg_rev);

  SVN_ERR(check_tree_conflict(&is_conflicted, b, dst_relpath, svn_node_dir,
                              svn_wc_conflict_action_edit,
                              scratch_pool));
  if (is_conflicted)
    return SVN_NO_ERROR;

  /* Get kind, revision, and checksum of the moved-here node. */
  SVN_ERR(svn_wc__db_depth_get_info(&status, &move_dst_kind, &move_dst_revision,
                                    &move_dst_repos_relpath, NULL, NULL, NULL,
                                    NULL, NULL, &old_version.checksum, NULL,
                                    NULL, &old_version.props,
                                    b->wcroot, dst_relpath,
                                    relpath_depth(b->move_root_dst_relpath),
                                    scratch_pool, scratch_pool));
  SVN_ERR_ASSERT(move_dst_revision == expected_move_dst_revision);
  SVN_ERR_ASSERT(move_dst_kind == svn_kind_dir);

  old_version.location_and_kind = b->old_version;
  new_version.location_and_kind = b->new_version;

  new_version.checksum = NULL; /* not a file */
  new_version.props = new_props ? new_props : old_version.props;

  if (new_props)
    {
      const char *dst_abspath = svn_dirent_join(b->wcroot->abspath,
                                                dst_relpath,
                                                scratch_pool);
      svn_wc_notify_state_t prop_state;
      svn_skel_t *conflict_skel = NULL;
      apr_hash_t *actual_props;
      apr_array_header_t *propchanges;

      SVN_ERR(update_working_props(&prop_state, &conflict_skel,
                                   &propchanges, &actual_props,
                                   b->db, dst_abspath,
                                   &old_version, &new_version,
                                   b->result_pool, scratch_pool));

      if (conflict_skel)
        {
          /* ### need to pass in the node kinds (before & after)? */
          SVN_ERR(create_conflict_markers(b->work_items, dst_abspath,
                                          b->db, move_dst_repos_relpath,
                                          conflict_skel, b->operation,
                                          &old_version, &new_version,
                                          b->result_pool, scratch_pool));
          SVN_ERR(svn_wc__db_mark_conflict_internal(b->wcroot, dst_relpath,
                                                    conflict_skel,
                                                    scratch_pool));
        }

      if (b->notify_func)
        SVN_ERR(update_move_list_add(b->wcroot, dst_relpath,
                                     svn_wc_notify_update_update,
                                     svn_node_dir,
                                     svn_wc_notify_state_inapplicable,
                                     prop_state));
    }

  return SVN_NO_ERROR;
}


/* Merge the difference between OLD_VERSION and NEW_VERSION into
 * the working file at LOCAL_RELPATH.
 *
 * The term 'old' refers to the pre-update state, which is the state of
 * (some layer of) LOCAL_RELPATH while this function runs; and 'new'
 * refers to the post-update state, as found at the (base layer of) the
 * move source path while this function runs.
 *
 * LOCAL_RELPATH is a file in the working copy at WCROOT in DB, and
 * REPOS_RELPATH is the repository path it would be committed to.
 *
 * Use NOTIFY_FUNC and NOTIFY_BATON for notifications.
 * Set *WORK_ITEMS to any required work items, allocated in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
update_working_file(svn_skel_t **work_items,
                    const char *local_relpath,
                    const char *repos_relpath,
                    svn_wc_operation_t operation,
                    const working_node_version_t *old_version,
                    const working_node_version_t *new_version,
                    svn_wc__db_wcroot_t *wcroot,
                    svn_wc__db_t *db,
                    svn_wc_notify_func2_t notify_func,
                    void *notify_baton,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  const char *local_abspath = svn_dirent_join(wcroot->abspath,
                                              local_relpath,
                                              scratch_pool);
  const char *old_pristine_abspath;
  const char *new_pristine_abspath;
  svn_skel_t *conflict_skel = NULL;
  apr_hash_t *actual_props;
  apr_array_header_t *propchanges;
  enum svn_wc_merge_outcome_t merge_outcome;
  svn_wc_notify_state_t prop_state, content_state;
  svn_skel_t *work_item;

  *work_items = NULL;

  SVN_ERR(update_working_props(&prop_state, &conflict_skel, &propchanges,
                               &actual_props, db, local_abspath,
                               old_version, new_version,
                               result_pool, scratch_pool));

  /*
   * Run a 3-way merge to update the file, using the pre-update
   * pristine text as the merge base, the post-update pristine
   * text as the merge-left version, and the current content of the
   * moved-here working file as the merge-right version.
   */
  SVN_ERR(svn_wc__db_pristine_get_path(&old_pristine_abspath,
                                       db, wcroot->abspath,
                                       old_version->checksum,
                                       scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__db_pristine_get_path(&new_pristine_abspath,
                                       db, wcroot->abspath,
                                       new_version->checksum,
                                       scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__internal_merge(&work_item, &conflict_skel,
                                 &merge_outcome, db,
                                 old_pristine_abspath,
                                 new_pristine_abspath,
                                 local_abspath,
                                 local_abspath,
                                 NULL, NULL, NULL, /* diff labels */
                                 actual_props,
                                 FALSE, /* dry-run */
                                 NULL, /* diff3-cmd */
                                 NULL, /* merge options */
                                 propchanges,
                                 NULL, NULL, /* cancel_func + baton */
                                 result_pool, scratch_pool));

  *work_items = svn_wc__wq_merge(*work_items, work_item, result_pool);

  /* If there are any conflicts to be stored, convert them into work items
   * too. */
  if (conflict_skel)
    {
      /* ### need to pass in the node kinds (before & after)? */
      SVN_ERR(create_conflict_markers(work_items, local_abspath, db,
                                      repos_relpath, conflict_skel,
                                      operation, old_version, new_version,
                                      result_pool, scratch_pool));
      SVN_ERR(svn_wc__db_mark_conflict_internal(wcroot, local_relpath,
                                                conflict_skel,
                                                scratch_pool));
    }

  if (merge_outcome == svn_wc_merge_conflict)
    {
      content_state = svn_wc_notify_state_conflicted;
    }
  else
    {
      svn_boolean_t is_locally_modified;

      SVN_ERR(svn_wc__internal_file_modified_p(&is_locally_modified,
                                               db, local_abspath,
                                               FALSE /* exact_comparison */,
                                               scratch_pool));
      if (is_locally_modified)
        content_state = svn_wc_notify_state_merged;
      else
        content_state = svn_wc_notify_state_changed;
    }

  if (notify_func)
    SVN_ERR(update_move_list_add(wcroot, local_relpath,
                                 svn_wc_notify_update_update,
                                 svn_node_file,
                                 content_state,
                                 prop_state));

  return SVN_NO_ERROR;
}


/* Edit the file found at the move destination, which is initially at
 * the old state.  Merge the changes into the "working"/"actual" file.
 */
static svn_error_t *
tc_editor_alter_file(void *baton,
                     const char *dst_relpath,
                     svn_revnum_t expected_move_dst_revision,
                     apr_hash_t *new_props,
                     const svn_checksum_t *new_checksum,
                     svn_stream_t *new_contents,
                     apr_pool_t *scratch_pool)
{
  struct tc_editor_baton *b = baton;
  const char *move_dst_repos_relpath;
  svn_revnum_t move_dst_revision;
  svn_kind_t move_dst_kind;
  working_node_version_t old_version, new_version;
  svn_boolean_t is_conflicted;

  SVN_ERR(check_tree_conflict(&is_conflicted, b, dst_relpath, svn_node_file,
                              svn_wc_conflict_action_edit,
                              scratch_pool));
  if (is_conflicted)
    return SVN_NO_ERROR;

  /* Get kind, revision, and checksum of the moved-here node. */
  SVN_ERR(svn_wc__db_depth_get_info(NULL, &move_dst_kind, &move_dst_revision,
                                    &move_dst_repos_relpath, NULL, NULL, NULL,
                                    NULL, NULL, &old_version.checksum, NULL,
                                    NULL, &old_version.props,
                                    b->wcroot, dst_relpath,
                                    relpath_depth(b->move_root_dst_relpath),
                                    scratch_pool, scratch_pool));
  SVN_ERR_ASSERT(move_dst_revision == expected_move_dst_revision);
  SVN_ERR_ASSERT(move_dst_kind == svn_kind_file);

  old_version.location_and_kind = b->old_version;
  new_version.location_and_kind = b->new_version;

  /* If new checksum is null that means no change; similarly props. */
  new_version.checksum = new_checksum ? new_checksum : old_version.checksum;
  new_version.props = new_props ? new_props : old_version.props;

  /* Update file and prop contents if the update has changed them. */
  if (!svn_checksum_match(new_checksum, old_version.checksum) || new_props)
    {
      svn_skel_t *work_items;

      SVN_ERR(update_working_file(&work_items, dst_relpath,
                                  move_dst_repos_relpath,
                                  b->operation, &old_version, &new_version,
                                  b->wcroot, b->db,
                                  b->notify_func, b->notify_baton,
                                  b->result_pool, scratch_pool));
      *b->work_items = svn_wc__wq_merge(*b->work_items, work_items,
                                        b->result_pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_alter_symlink(void *baton,
                        const char *relpath,
                        svn_revnum_t revision,
                        apr_hash_t *props,
                        const char *target,
                        apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
}

static svn_error_t *
tc_editor_delete(void *baton,
                 const char *relpath,
                 svn_revnum_t revision,
                 apr_pool_t *scratch_pool)
{
  struct tc_editor_baton *b = baton;
  svn_sqlite__stmt_t *stmt;
  int op_depth = relpath_depth(b->move_root_dst_relpath);
  svn_boolean_t is_conflicted;
  const char *local_abspath = svn_dirent_join(b->wcroot->abspath, relpath,
                                              scratch_pool);

  /* Check before retracting delete to catch delete-delete
     conflicts. This catches conflicts on the node itself; deleted
     children are caught as local modifications below.*/
  SVN_ERR(check_tree_conflict(&is_conflicted, b, relpath, svn_node_unknown,
                              svn_wc_conflict_action_delete,
                              scratch_pool));

  if (!is_conflicted)
    {
      svn_boolean_t is_modified, is_all_deletes;

      SVN_ERR(svn_wc__node_has_local_mods(&is_modified, &is_all_deletes, b->db,
                                          local_abspath,
                                          NULL, NULL, scratch_pool));
      if (is_modified)
        {
          svn_wc_conflict_reason_t reason;

          if (!is_all_deletes)
            {
              /* No conflict means no NODES rows at the relpath op-depth
                 so it's easy to convert the modified tree into a copy. */
              SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                              STMT_UPDATE_OP_DEPTH_RECURSIVE));
              SVN_ERR(svn_sqlite__bindf(stmt, "isdd", b->wcroot->wc_id, relpath,
                                        op_depth, relpath_depth(relpath)));
              SVN_ERR(svn_sqlite__step_done(stmt));

              reason = svn_wc_conflict_reason_edited;
              is_conflicted = TRUE;
            }
          else
            {

              SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                          STMT_DELETE_WORKING_OP_DEPTH_ABOVE));
              SVN_ERR(svn_sqlite__bindf(stmt, "isd", b->wcroot->wc_id, relpath,
                                        op_depth));
              SVN_ERR(svn_sqlite__step_done(stmt));

              reason = svn_wc_conflict_reason_deleted;
              /* Don't set is_conflicted so working files/dirs are deleted. */
            }
          SVN_ERR(mark_tree_conflict(b, relpath,
                                     /* ### kinds? */
                                     svn_node_dir, svn_node_dir, reason,
                                     svn_wc_conflict_action_delete, NULL,
                                     scratch_pool));
          if (b->notify_func)
            SVN_ERR(update_move_list_add(b->wcroot, relpath,
                                         svn_wc_notify_tree_conflict,
                                         svn_node_dir, /* ### kinds? */
                                         svn_wc_notify_state_inapplicable,
                                         svn_wc_notify_state_inapplicable));
        }
    }

  if (!is_conflicted)
    {
      svn_boolean_t have_row;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      svn_skel_t *work_item;
      svn_kind_t del_kind;
      const char *del_abspath;

      SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                        STMT_SELECT_CHILDREN_OP_DEPTH));
      SVN_ERR(svn_sqlite__bindf(stmt, "isd", b->wcroot->wc_id, relpath,
                                op_depth));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      while (have_row)
        {
          svn_error_t *err;

          svn_pool_clear(iterpool);

          del_kind = svn_sqlite__column_token(stmt, 1, kind_map);
          del_abspath = svn_dirent_join(b->wcroot->abspath,
                                        svn_sqlite__column_text(stmt, 0, NULL),
                                        iterpool);
          if (del_kind == svn_kind_dir)
            err = svn_wc__wq_build_dir_remove(&work_item, b->db,
                                              b->wcroot->abspath, del_abspath,
                                              FALSE /* recursive */,
                                              iterpool, iterpool);
          else
            err = svn_wc__wq_build_file_remove(&work_item, b->db,
                                               b->wcroot->abspath, del_abspath,
                                               iterpool, iterpool);
          if (!err)
            err = svn_wc__db_wq_add(b->db, b->wcroot->abspath, work_item,
                                    iterpool);
          if (err)
            return svn_error_compose_create(err, svn_sqlite__reset(stmt));

          SVN_ERR(svn_sqlite__step(&have_row, stmt));
        }
      SVN_ERR(svn_sqlite__reset(stmt));

      SVN_ERR(svn_wc__db_depth_get_info(NULL, &del_kind, NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL,
                                        b->wcroot, relpath, op_depth,
                                        iterpool, iterpool));
      if (del_kind == svn_kind_dir)
        SVN_ERR(svn_wc__wq_build_dir_remove(&work_item, b->db,
                                            b->wcroot->abspath, local_abspath,
                                            FALSE /* recursive */,
                                            iterpool, iterpool));
      else
        SVN_ERR(svn_wc__wq_build_file_remove(&work_item, b->db,
                                             b->wcroot->abspath, local_abspath,
                                             iterpool, iterpool));
      SVN_ERR(svn_wc__db_wq_add(b->db, b->wcroot->abspath, work_item,
                                iterpool));
      svn_pool_destroy(iterpool);
    }

  /* Deleting the ROWS is valid so long as we update the parent before
     committing the transaction. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                    STMT_DELETE_WORKING_OP_DEPTH));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", b->wcroot->wc_id, relpath, op_depth));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Retract any base-delete. */
  SVN_ERR(svn_wc__db_retract_parent_delete(b->wcroot, relpath, op_depth,
                                           scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_copy(void *baton,
               const char *src_relpath,
               svn_revnum_t src_revision,
               const char *dst_relpath,
               svn_revnum_t replaces_rev,
               apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
}

static svn_error_t *
tc_editor_move(void *baton,
               const char *src_relpath,
               svn_revnum_t src_revision,
               const char *dst_relpath,
               svn_revnum_t replaces_rev,
               apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
}

static svn_error_t *
tc_editor_rotate(void *baton,
                 const apr_array_header_t *relpaths,
                 const apr_array_header_t *revisions,
                 apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
}

static svn_error_t *
tc_editor_complete(void *baton,
                   apr_pool_t *scratch_pool)
{
  struct tc_editor_baton *b = baton;

  /* Send all queued up notifications. */
  SVN_ERR(update_move_list_notify(b->wcroot,
                                  b->old_version->peg_rev,
                                  b->new_version->peg_rev,
                                  b->notify_func, b->notify_baton,
                                  scratch_pool));
  if (b->notify_func)
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(svn_dirent_join(b->wcroot->abspath,
                                                    b->move_root_dst_relpath,
                                                    scratch_pool),
                                    svn_wc_notify_update_completed,
                                    scratch_pool);
      notify->kind = svn_node_none;
      notify->content_state = svn_wc_notify_state_inapplicable;
      notify->prop_state = svn_wc_notify_state_inapplicable;
      notify->revision = b->new_version->peg_rev;
      b->notify_func(b->notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_abort(void *baton,
                apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* The editor callback table implementing the receiver. */
static const svn_editor_cb_many_t editor_ops = {
  tc_editor_add_directory,
  tc_editor_add_file,
  tc_editor_add_symlink,
  tc_editor_add_absent,
  tc_editor_alter_directory,
  tc_editor_alter_file,
  tc_editor_alter_symlink,
  tc_editor_delete,
  tc_editor_copy,
  tc_editor_move,
  tc_editor_rotate,
  tc_editor_complete,
  tc_editor_abort
};


/*
 * Driver code.
 *
 * The scenario is that a subtree has been locally moved, and then the base
 * layer on the source side of the move has received an update to a new
 * state.  The destination subtree has not yet been updated, and still
 * matches the pre-update state of the source subtree.
 *
 * The edit driver drives the receiver with the difference between the
 * pre-update state (as found now at the move-destination) and the
 * post-update state (found now at the move-source).
 *
 * We currently assume that both the pre-update and post-update states are
 * single-revision.
 */

/* Set *OPERATION, *LOCAL_CHANGE, *INCOMING_CHANGE, *OLD_VERSION, *NEW_VERSION
 * to reflect the tree conflict on the victim SRC_ABSPATH in DB.
 *
 * If SRC_ABSPATH is not a tree-conflict victim, return an error.
 */
static svn_error_t *
get_tc_info(svn_wc_operation_t *operation,
            svn_wc_conflict_reason_t *local_change,
            svn_wc_conflict_action_t *incoming_change,
            svn_wc_conflict_version_t **old_version,
            svn_wc_conflict_version_t **new_version,
            svn_wc__db_t *db,
            const char *src_abspath,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  const apr_array_header_t *locations;
  svn_boolean_t tree_conflicted;
  svn_skel_t *conflict_skel;

  /* ### Check for mixed-rev src or dst? */

  /* Check for tree conflict on src. */
  SVN_ERR(svn_wc__db_read_conflict(&conflict_skel, db,
                                   src_abspath,
                                   scratch_pool, scratch_pool));
  if (!conflict_skel)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("'%s' is not in conflict"),
                             svn_dirent_local_style(src_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__conflict_read_info(operation, &locations,
                                     NULL, NULL, &tree_conflicted,
                                     db, src_abspath,
                                     conflict_skel, result_pool,
                                     scratch_pool));
  if (!tree_conflicted)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("'%s' is not a tree-conflict victim"),
                             svn_dirent_local_style(src_abspath,
                                                    scratch_pool));
  if (locations)
    {
      SVN_ERR_ASSERT(locations->nelts >= 2);
      *old_version = APR_ARRAY_IDX(locations, 0,
                                     svn_wc_conflict_version_t *);
      *new_version = APR_ARRAY_IDX(locations, 1,
                                   svn_wc_conflict_version_t *);
    }

  SVN_ERR(svn_wc__conflict_read_tree_conflict(local_change,
                                              incoming_change,
                                              db, src_abspath,
                                              conflict_skel, scratch_pool,
                                              scratch_pool));

  return SVN_NO_ERROR;
}

/* Return *PROPS, *CHECKSUM, *CHILDREN and *KIND for LOCAL_RELPATH at
   OP_DEPTH provided the row exists.  Return *KIND of svn_kind_none if
   the row does not exist. *CHILDREN is a sorted array of basenames of
   type 'const char *', rather than a hash, to allow the driver to
   process children in a defined order. */
static svn_error_t *
get_info(apr_hash_t **props,
         const svn_checksum_t **checksum,
         apr_array_header_t **children,
         svn_kind_t *kind,
         const char *local_relpath,
         int op_depth,
         svn_wc__db_wcroot_t *wcroot,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  apr_hash_t *hash_children;
  apr_array_header_t *sorted_children;
  svn_error_t *err;
  int i;

  err = svn_wc__db_depth_get_info(NULL, kind, NULL, NULL, NULL, NULL, NULL,
                                  NULL, NULL, checksum, NULL, NULL, props,
                                  wcroot, local_relpath, op_depth,
                                  result_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      *kind = svn_kind_none;
    }
  else
    SVN_ERR(err);

  
  SVN_ERR(svn_wc__db_get_children_op_depth(&hash_children, wcroot,
                                           local_relpath, op_depth,
                                           scratch_pool, scratch_pool));

  sorted_children = svn_sort__hash(hash_children,
                                   svn_sort_compare_items_lexically,
                                   scratch_pool);

  *children = apr_array_make(result_pool, sorted_children->nelts,
                             sizeof(const char *));
  for (i = 0; i < sorted_children->nelts; ++i)
    APR_ARRAY_PUSH(*children, const char *)
      = apr_pstrdup(result_pool, APR_ARRAY_IDX(sorted_children, i,
                                               svn_sort__item_t).key);
                                                           
  return SVN_NO_ERROR;
}

/* Return TRUE if SRC_CHILDREN and DST_CHILDREN represent the same
   children, FALSE otherwise.  SRC_CHILDREN and DST_CHILDREN are
   sorted arrays of basenames of type 'const char *'. */
static svn_boolean_t
children_match(apr_array_header_t *src_children,
               apr_array_header_t *dst_children) { int i;

  if (src_children->nelts != dst_children->nelts)
    return FALSE;

  for(i = 0; i < src_children->nelts; ++i)
    {
      const char *src_child =
        APR_ARRAY_IDX(src_children, i, const char *);
      const char *dst_child =
        APR_ARRAY_IDX(dst_children, i, const char *);

      if (strcmp(src_child, dst_child))
        return FALSE;
    }

  return TRUE;
}

/* Return TRUE if SRC_PROPS and DST_PROPS contain the same properties,
   FALSE otherwise. SRC_PROPS and DST_PROPS are standard property
   hashes. */
static svn_error_t *
props_match(svn_boolean_t *match,
            apr_hash_t *src_props,
            apr_hash_t *dst_props,
            apr_pool_t *scratch_pool)
{
  if (!src_props && !dst_props)
    *match = TRUE;
  else if (!src_props || ! dst_props)
    *match = FALSE;
  else
    {
      apr_array_header_t *propdiffs;

      SVN_ERR(svn_prop_diffs(&propdiffs, src_props, dst_props, scratch_pool));
      *match = propdiffs->nelts ? FALSE : TRUE;
    }
  return SVN_NO_ERROR;
}

/* ### Drive TC_EDITOR so as to ...
 */
static svn_error_t *
update_moved_away_node(svn_editor_t *tc_editor,
                       const char *src_relpath,
                       const char *dst_relpath,
                       int src_op_depth,
                       const char *move_root_dst_relpath,
                       svn_revnum_t move_root_dst_revision,
                       svn_wc__db_t *db,
                       svn_wc__db_wcroot_t *wcroot,
                       apr_pool_t *scratch_pool)
{
  svn_kind_t src_kind, dst_kind;
  const svn_checksum_t *src_checksum, *dst_checksum;
  apr_hash_t *src_props, *dst_props;
  apr_array_header_t *src_children, *dst_children;
  int dst_op_depth = relpath_depth(move_root_dst_relpath);

  SVN_ERR(get_info(&src_props, &src_checksum, &src_children, &src_kind,
                   src_relpath, src_op_depth,
                   wcroot, scratch_pool, scratch_pool));

  SVN_ERR(get_info(&dst_props, &dst_checksum, &dst_children, &dst_kind,
                   dst_relpath, dst_op_depth,
                   wcroot, scratch_pool, scratch_pool));

  if (src_kind == svn_kind_none
      || (dst_kind != svn_kind_none && src_kind != dst_kind))
    {
      SVN_ERR(svn_editor_delete(tc_editor, dst_relpath,
                                move_root_dst_revision));
    }
 
  if (src_kind != svn_kind_none && src_kind != dst_kind)
    {
      if (src_kind == svn_kind_file || src_kind == svn_kind_symlink)
        {
          svn_stream_t *contents;

          SVN_ERR(svn_wc__db_pristine_read(&contents, NULL, db,
                                           wcroot->abspath, src_checksum,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_editor_add_file(tc_editor, dst_relpath,
                                      src_checksum, contents, src_props,
                                      move_root_dst_revision));
        }
      else if (src_kind == svn_kind_dir)
        {
          SVN_ERR(svn_editor_add_directory(tc_editor, dst_relpath,
                                           src_children, src_props,
                                           move_root_dst_revision));
        }
    }
  else if (src_kind != svn_kind_none)
    {
      svn_boolean_t match;
      apr_hash_t *props;

      SVN_ERR(props_match(&match, src_props, dst_props, scratch_pool));
      props = match ? NULL: src_props;

      
      if (src_kind == svn_kind_file || src_kind == svn_kind_symlink)
        {
          svn_stream_t *contents;

          if (svn_checksum_match(src_checksum, dst_checksum))
            src_checksum = NULL;

          if (src_checksum)
            SVN_ERR(svn_wc__db_pristine_read(&contents, NULL, db,
                                             wcroot->abspath, src_checksum,
                                             scratch_pool, scratch_pool));
          else
            contents = NULL;

          if (props || src_checksum)
            SVN_ERR(svn_editor_alter_file(tc_editor, dst_relpath,
                                          move_root_dst_revision,
                                          props, src_checksum, contents));
        }
      else if (src_kind == svn_kind_dir)
        {
          apr_array_header_t *children
            = children_match(src_children, dst_children) ? NULL : src_children;

          if (props || children)
            SVN_ERR(svn_editor_alter_directory(tc_editor, dst_relpath,
                                               move_root_dst_revision,
                                               children, props));
        }
    }

  if (src_kind == svn_kind_dir)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      int i = 0, j = 0;

      while (i < src_children->nelts || j < dst_children->nelts)
        {
          const char *child_name;
          const char *src_child_relpath, *dst_child_relpath;
          svn_boolean_t src_only = FALSE, dst_only = FALSE;

          svn_pool_clear(iterpool);
          if (i >= src_children->nelts)
            {
              dst_only = TRUE;
              child_name = APR_ARRAY_IDX(dst_children, j, const char *);
            }
          else if (j >= dst_children->nelts)
            {
              src_only = TRUE;
              child_name = APR_ARRAY_IDX(src_children, i, const char *);
            }
          else
            {
              const char *src_name = APR_ARRAY_IDX(src_children, i,
                                                   const char *);
              const char *dst_name = APR_ARRAY_IDX(dst_children, j,
                                                   const char *);
              int cmp = strcmp(src_name, dst_name);

              if (cmp > 0)
                dst_only = TRUE;
              else if (cmp < 0)
                src_only = TRUE;

              child_name = dst_only ? dst_name : src_name;
            }

          src_child_relpath = svn_relpath_join(src_relpath, child_name,
                                               iterpool);
          dst_child_relpath = svn_relpath_join(dst_relpath, child_name,
                                               iterpool);

          SVN_ERR(update_moved_away_node(tc_editor, src_child_relpath,
                                         dst_child_relpath, src_op_depth,
                                         move_root_dst_relpath,
                                         move_root_dst_revision,
                                         db, wcroot, scratch_pool));

          if (!dst_only)
            ++i;
          if (!src_only)
            ++j;
        }
    }

  return SVN_NO_ERROR;
}

/* Update the single op-depth layer in the move destination subtree
   rooted at DST_RELPATH to make it match the move source subtree
   rooted at SRC_RELPATH. */
static svn_error_t *
replace_moved_layer(const char *src_relpath,
                    const char *dst_relpath,
                    int src_op_depth,
                    svn_wc__db_wcroot_t *wcroot,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int dst_op_depth = relpath_depth(dst_relpath);

  /* Replace entire subtree at one op-depth. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_LOCAL_RELPATH_OP_DEPTH));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id,
                            src_relpath, src_op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      svn_error_t *err;
      svn_sqlite__stmt_t *stmt2;
      const char *src_cp_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      const char *dst_cp_relpath
        = svn_relpath_join(dst_relpath,
                           svn_relpath_skip_ancestor(src_relpath,
                                                     src_cp_relpath),
                           scratch_pool);

      err = svn_sqlite__get_statement(&stmt2, wcroot->sdb,
                                      STMT_COPY_NODE_MOVE);
      if (!err)
        err = svn_sqlite__bindf(stmt2, "isdsds", wcroot->wc_id,
                                src_cp_relpath, src_op_depth,
                                dst_cp_relpath, dst_op_depth,
                                svn_relpath_dirname(dst_cp_relpath,
                                                    scratch_pool));
      if (!err)
        err = svn_sqlite__step_done(stmt2);
      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));
 
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

/* Transfer changes from the move source to the move destination.
 *
 * Drive the editor TC_EDITOR with the difference between DST_RELPATH
 * (at its own op-depth) and SRC_RELPATH (at op-depth zero).
 *
 * Then update the single op-depth layer in the move destination subtree
 * rooted at DST_RELPATH to make it match the move source subtree
 * rooted at SRC_RELPATH.
 *
 * ### And the other params?
 */
static svn_error_t *
drive_tree_conflict_editor(svn_editor_t *tc_editor,
                           const char *src_relpath,
                           const char *dst_relpath,
                           int src_op_depth,
                           svn_wc_operation_t operation,
                           svn_wc_conflict_reason_t local_change,
                           svn_wc_conflict_action_t incoming_change,
                           svn_wc_conflict_version_t *old_version,
                           svn_wc_conflict_version_t *new_version,
                           svn_wc__db_t *db,
                           svn_wc__db_wcroot_t *wcroot,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *scratch_pool)
{
  /*
   * Refuse to auto-resolve unsupported tree conflicts.
   */
  /* ### Only handle conflicts created by update/switch operations for now. */
  if (operation != svn_wc_operation_update &&
      operation != svn_wc_operation_switch)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                            _("Cannot auto-resolve tree-conflict on '%s'"),
                            svn_dirent_local_style(
                              svn_dirent_join(wcroot->abspath,
                                              src_relpath, scratch_pool),
                              scratch_pool));

  /* We walk the move source (i.e. the post-update tree), comparing each node
   * with the equivalent node at the move destination and applying the update
   * to nodes at the move destination. */
  SVN_ERR(update_moved_away_node(tc_editor, src_relpath, dst_relpath,
                                 src_op_depth,
                                 dst_relpath, old_version->peg_rev,
                                 db, wcroot, scratch_pool));

  SVN_ERR(replace_moved_layer(src_relpath, dst_relpath, src_op_depth,
                              wcroot, scratch_pool));

  SVN_ERR(svn_editor_complete(tc_editor));

  return SVN_NO_ERROR;
}

/* The body of svn_wc__db_update_moved_away_conflict_victim(), which see.
 */
static svn_error_t *
update_moved_away_conflict_victim(svn_skel_t **work_items,
                                  svn_wc__db_t *db,
                                  svn_wc__db_wcroot_t *wcroot,
                                  const char *victim_relpath,
                                  svn_wc_operation_t operation,
                                  svn_wc_conflict_reason_t local_change,
                                  svn_wc_conflict_action_t incoming_change,
                                  svn_wc_conflict_version_t *old_version,
                                  svn_wc_conflict_version_t *new_version,
                                  svn_wc_notify_func2_t notify_func,
                                  void *notify_baton,
                                  svn_cancel_func_t cancel_func,
                                  void *cancel_baton,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_editor_t *tc_editor;
  struct tc_editor_baton *tc_editor_baton;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int src_op_depth;

  /* ### assumes wc write lock already held */

  /* Construct editor baton. */
  tc_editor_baton = apr_pcalloc(scratch_pool, sizeof(*tc_editor_baton));
  SVN_ERR(svn_wc__db_scan_deletion_internal(
            NULL, &tc_editor_baton->move_root_dst_relpath, NULL, NULL,
            wcroot, victim_relpath, scratch_pool, scratch_pool));
  if (tc_editor_baton->move_root_dst_relpath == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("The node '%s' has not been moved away"),
                             svn_dirent_local_style(
                               svn_dirent_join(wcroot->abspath, victim_relpath,
                                               scratch_pool),
                               scratch_pool));
  tc_editor_baton->operation = operation;
  tc_editor_baton->old_version= old_version;
  tc_editor_baton->new_version= new_version;
  tc_editor_baton->db = db;
  tc_editor_baton->wcroot = wcroot;
  tc_editor_baton->work_items = work_items;
  tc_editor_baton->notify_func = notify_func;
  tc_editor_baton->notify_baton = notify_baton;
  tc_editor_baton->result_pool = result_pool;

  /* ### TODO get from svn_wc__db_scan_deletion_internal? */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, victim_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    src_op_depth = svn_sqlite__column_int(stmt, 0);
  SVN_ERR(svn_sqlite__reset(stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("'%s' is not deleted"),
                             svn_dirent_local_style(
                               svn_dirent_join(wcroot->abspath, victim_relpath,
                                               scratch_pool),
                               scratch_pool));

  /* Create a new, and empty, list for notification information. */
  SVN_ERR(svn_sqlite__exec_statements(wcroot->sdb,
                                      STMT_CREATE_UPDATE_MOVE_LIST));
  /* Create the editor... */
  SVN_ERR(svn_editor_create(&tc_editor, tc_editor_baton,
                            cancel_func, cancel_baton,
                            scratch_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(tc_editor, &editor_ops, scratch_pool));

  /* ... and drive it. */
  SVN_ERR(drive_tree_conflict_editor(tc_editor,
                                     victim_relpath,
                                     tc_editor_baton->move_root_dst_relpath,
                                     src_op_depth,
                                     operation,
                                     local_change, incoming_change,
                                     tc_editor_baton->old_version,
                                     tc_editor_baton->new_version,
                                     db, wcroot,
                                     cancel_func, cancel_baton,
                                     scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_update_moved_away_conflict_victim(svn_skel_t **work_items,
                                             svn_wc__db_t *db,
                                             const char *victim_abspath,
                                             svn_wc_notify_func2_t notify_func,
                                             void *notify_baton,
                                             svn_cancel_func_t cancel_func,
                                             void *cancel_baton,
                                             apr_pool_t *result_pool,
                                             apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_wc_operation_t operation;
  svn_wc_conflict_reason_t local_change;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_version_t *old_version;
  svn_wc_conflict_version_t *new_version;

  SVN_ERR(get_tc_info(&operation, &local_change, &incoming_change,
                      &old_version, &new_version,
                      db, victim_abspath,
                      scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, victim_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_WC__DB_WITH_TXN(
    update_moved_away_conflict_victim(
      work_items, db, wcroot, local_relpath,
      operation, local_change, incoming_change,
      old_version, new_version,
      notify_func, notify_baton,
      cancel_func, cancel_baton,
      result_pool, scratch_pool),
    wcroot);

  return SVN_NO_ERROR;
}

/* Bump LOCAL_RELPATH, and all the children of LOCAL_RELPATH, that are
   moved-to at op-depth greater than OP_DEPTH.  SRC_DONE is a hash
   with keys that are 'const char *' relpaths that have already been
   bumped.  Any bumped paths are added to SRC_DONE. */
static svn_error_t *
bump_moved_away(svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath,
                int op_depth,
                apr_hash_t *src_done,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_MOVED_PAIR3));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", wcroot->wc_id, local_relpath,
                            op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while(have_row)
    {
      svn_sqlite__stmt_t *stmt2;
      const char *src_relpath, *dst_relpath;
      int src_op_depth = svn_sqlite__column_int(stmt, 2);
      svn_error_t *err;
      svn_skel_t *conflict;

      svn_pool_clear(iterpool);

      src_relpath = svn_sqlite__column_text(stmt, 0, iterpool);
      dst_relpath = svn_sqlite__column_text(stmt, 1, iterpool);

      err = svn_sqlite__get_statement(&stmt2, wcroot->sdb,
                                      STMT_HAS_LAYER_BETWEEN);
      if (!err)
       err = svn_sqlite__bindf(stmt2, "isdd", wcroot->wc_id, local_relpath,
                               op_depth, src_op_depth);
      if (!err)
        err = svn_sqlite__step(&have_row, stmt2);
      if (!err)
        err = svn_sqlite__reset(stmt2);
      if (!err && !have_row)
        {
          const char *src_root_relpath = src_relpath;

          while (relpath_depth(src_root_relpath) > src_op_depth)
            src_root_relpath = svn_relpath_dirname(src_root_relpath, iterpool);

          if (!apr_hash_get(src_done, src_relpath, APR_HASH_KEY_STRING))
            {
              apr_hash_set(src_done, apr_pstrdup(result_pool, src_relpath),
                           APR_HASH_KEY_STRING, "");
              err = svn_wc__db_read_conflict_internal(&conflict, wcroot,
                                                      src_root_relpath,
                                                      iterpool, iterpool);
              /* ### TODO: check this is the right sort of tree-conflict? */
              if (!err && !conflict)
                {
                  /* ### TODO: verify moved_here? */
                  err = replace_moved_layer(src_relpath, dst_relpath, op_depth,
                                            wcroot, iterpool);

                  if (!err)
                    err = bump_moved_away(wcroot, dst_relpath,
                                          relpath_depth(dst_relpath), src_done,
                                          result_pool, iterpool);
                }
            }
        }

      if (err)
        return svn_error_compose_create(err, svn_sqlite__reset(stmt));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_bump_moved_away(svn_wc__db_wcroot_t *wcroot,
                           const char *local_relpath,
                           svn_depth_t depth,
                           apr_pool_t *scratch_pool)
{
  apr_hash_t *src_done;

  /* ### TODO: raise tree-conflicts? */
  if (depth != svn_depth_infinity)
    return SVN_NO_ERROR;

  src_done = apr_hash_make(scratch_pool);
  SVN_ERR(bump_moved_away(wcroot, local_relpath, 0, src_done,
                          scratch_pool, scratch_pool));

  return SVN_NO_ERROR;
}
