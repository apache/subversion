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

/* This editor is used during resolution of tree conflicts.
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
 * After the intial move an update, or this editor for trees that have
 * been moved more than once, has modified the NODES in the move
 * source, and introduced a tree-conflict since the source and
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

#include "svn_checksum.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_props.h"
#include "svn_pools.h"

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
  svn_wc_conflict_version_t *old_version;
  svn_wc_conflict_version_t *new_version;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  apr_pool_t *result_pool;
};

/* If LOCAL_RELPATH is shadowed then raise a tree-conflict on the root
   of the obstruction if such a tree-conflict does not already exist.

   KIND is the node kind of ... ### what?

   Set *IS_CONFLICTED ... ### if/iff what?
 */
static svn_error_t *
check_tree_conflict(svn_boolean_t *is_conflicted,
                    struct tc_editor_baton *b,
                    const char *local_relpath,
                    svn_node_kind_t kind,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  int dst_op_depth = relpath_depth(b->move_root_dst_relpath);
  int op_depth;
  const char *conflict_root_relpath = local_relpath;
  const char *moved_to_relpath;
  svn_skel_t *conflict;
  svn_wc_conflict_version_t *version;

  SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                    STMT_SELECT_LOWEST_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", b->wcroot->wc_id, local_relpath,
                            dst_op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    op_depth = svn_sqlite__column_int(stmt, 0);
  SVN_ERR(svn_sqlite__reset(stmt));

  if (!have_row)
    {
      *is_conflicted = FALSE;
      return SVN_NO_ERROR;
    }

  *is_conflicted = TRUE;

  while (relpath_depth(conflict_root_relpath) > op_depth)
    conflict_root_relpath = svn_relpath_dirname(conflict_root_relpath,
                                                scratch_pool);

  SVN_ERR(svn_wc__db_read_conflict_internal(&conflict, b->wcroot,
                                            conflict_root_relpath,
                                            scratch_pool, scratch_pool));

  if (conflict)
    /* ### TODO: check this is the right sort of tree-conflict? */
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_scan_deletion_internal(NULL, &moved_to_relpath,
                                            NULL, NULL,
                                            b->wcroot, conflict_root_relpath,
                                            scratch_pool, scratch_pool));

  conflict = svn_wc__conflict_skel_create(scratch_pool);
  SVN_ERR(svn_wc__conflict_skel_add_tree_conflict(
                     conflict, NULL,
                     svn_dirent_join(b->wcroot->abspath, conflict_root_relpath,
                                     scratch_pool),
                     (moved_to_relpath
                      ? svn_wc_conflict_reason_moved_away
                      : svn_wc_conflict_reason_deleted),
                     svn_wc_conflict_action_edit,
                     scratch_pool,
                     scratch_pool));

  version = svn_wc_conflict_version_create2(b->old_version->repos_url,
                                            b->old_version->repos_uuid,
                                            local_relpath /* ### need *repos* relpath */,
                                            b->old_version->peg_rev,
                                            kind /* ### need *old* kind for this node */,
                                            scratch_pool);

  /* What about switch? */
  SVN_ERR(svn_wc__conflict_skel_set_op_update(conflict, version, NULL /* ### derive from b->new_version & new kind? */,
                                              scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__db_mark_conflict_internal(b->wcroot, conflict_root_relpath,
                                            conflict, scratch_pool));

  return SVN_NO_ERROR;
}

/* Mark a unversioned-add tree-conflict on RELPATH. */
static svn_error_t *
mark_unversioned_add_conflict(struct tc_editor_baton *b,
                              const char *relpath,
                              svn_node_kind_t kind,
                              apr_pool_t *scratch_pool)
{
  svn_skel_t *conflict = svn_wc__conflict_skel_create(scratch_pool);
  svn_wc_conflict_version_t *version;

  SVN_ERR(svn_wc__conflict_skel_add_tree_conflict(
                     conflict, NULL,
                     svn_dirent_join(b->wcroot->abspath, relpath,
                                     scratch_pool),
                     svn_wc_conflict_reason_unversioned,
                     svn_wc_conflict_action_add,
                     scratch_pool,
                     scratch_pool));

  version = svn_wc_conflict_version_create2(b->old_version->repos_url,
                                            b->old_version->repos_uuid,
                                            relpath /* ### need *repos* relpath */,
                                            b->old_version->peg_rev,
                                            kind /* ### need *old* kind for this node */,
                                            scratch_pool);

  /* ### How about switch? */
  SVN_ERR(svn_wc__conflict_skel_set_op_update(conflict, version,
                                              NULL /* ### derive from b->new_version & new kind? */,
                                              scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__db_mark_conflict_internal(b->wcroot, relpath,
                                            conflict, scratch_pool));

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

  /* Update NODES, only the bits not covered by the later call to
     replace_moved_layer. */
  SVN_ERR(svn_wc__db_extend_parent_delete(b->wcroot, relpath, svn_kind_dir,
                                          op_depth, scratch_pool));

  /* Check for NODES tree-conflict. */
  SVN_ERR(check_tree_conflict(&is_conflicted, b, relpath, svn_node_dir,
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
      SVN_ERR(mark_unversioned_add_conflict(b, relpath, svn_node_dir,
                                            scratch_pool));
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
                              scratch_pool));
  if (is_conflicted)
    return SVN_NO_ERROR;

  /* Check for unversioned tree-conflict */
  abspath = svn_dirent_join(b->wcroot->abspath, relpath, scratch_pool);
  SVN_ERR(svn_io_check_path(abspath, &kind, scratch_pool));

  if (kind != svn_node_none)
    {
      SVN_ERR(mark_unversioned_add_conflict(b, relpath, svn_node_file,
                                            scratch_pool));
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
  SVN_ERR(svn_wc__conflict_skel_set_op_update(conflict_skel,
                                              original_version,
                                              NULL /* ### derive from new_version & new kind? */,
                                              scratch_pool,
                                              scratch_pool));
  /* According to this func's doc string, it is "Currently only used for
   * property conflicts as text conflict markers are just in-wc files." */
  SVN_ERR(svn_wc__conflict_create_markers(&work_item, db,
                                          local_abspath,
                                          conflict_skel,
                                          scratch_pool,
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
                                          conflict_skel,
                                          &old_version, &new_version,
                                          b->result_pool, scratch_pool));
          SVN_ERR(svn_wc__db_mark_conflict_internal(b->wcroot, dst_relpath,
                                                    conflict_skel,
                                                    scratch_pool));
        }

      if (b->notify_func)
        {
          svn_wc_notify_t *notify;

          notify = svn_wc_create_notify(dst_abspath,
                                        svn_wc_notify_update_update,
                                        scratch_pool);
          notify->kind = svn_node_dir;
          notify->content_state = svn_wc_notify_state_inapplicable;
          notify->prop_state = prop_state;
          notify->old_revision = b->old_version->peg_rev;
          notify->revision = b->new_version->peg_rev;
          b->notify_func(b->notify_baton, notify, scratch_pool);
        }
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

  SVN_ERR(update_working_props(&prop_state, &conflict_skel, &propchanges,
                               &actual_props, db, local_abspath,
                               old_version, new_version,
                               result_pool, scratch_pool));

  /* If there are any conflicts to be stored, convert them into work items
   * too. */
  if (conflict_skel)
    {
      /* ### need to pass in the node kinds (before & after)? */
      SVN_ERR(create_conflict_markers(work_items, local_abspath, db,
                                      repos_relpath, conflict_skel,
                                      old_version, new_version,
                                      result_pool, scratch_pool));
    }

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
  SVN_ERR(svn_wc__internal_merge(work_items, &conflict_skel,
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

  /* If there are any conflicts to be stored, convert them into work items
   * too. */
  if (conflict_skel)
    {
      /* ### need to pass in the node kinds (before & after)? */
      SVN_ERR(create_conflict_markers(work_items, local_abspath, db,
                                      repos_relpath, conflict_skel,
                                      old_version, new_version,
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
    {
      svn_wc_notify_t *notify;

      notify = svn_wc_create_notify(local_abspath,
                                    svn_wc_notify_update_update,
                                    scratch_pool);
      notify->kind = svn_node_file;
      notify->content_state = content_state;
      notify->prop_state = prop_state;
      notify->old_revision = old_version->location_and_kind->peg_rev;
      notify->revision = new_version->location_and_kind->peg_rev;
      notify_func(notify_baton, notify, scratch_pool);
    }

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

  /* ### TODO update revision etc. in NODES table */

  /* Update file and prop contents if the update has changed them. */
  if (!svn_checksum_match(new_checksum, old_version.checksum)
      /* ### || props have changed */)
    {
      svn_skel_t *work_item;

      SVN_ERR(update_working_file(&work_item, dst_relpath,
                                  move_dst_repos_relpath,
                                  &old_version, &new_version,
                                  b->wcroot, b->db,
                                  b->notify_func, b->notify_baton,
                                  b->result_pool, scratch_pool));
      *b->work_items = svn_wc__wq_merge(*b->work_items, work_item,
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

  /* Deleting the ROWS is valid so long as we update the parent before
     committing the transaction. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                    STMT_DELETE_WORKING_OP_DEPTH));
  SVN_ERR(svn_sqlite__bindf(stmt, "isd", b->wcroot->wc_id, relpath, op_depth));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Retract any base-delete. */
  SVN_ERR(svn_wc__db_retract_parent_delete(b->wcroot, relpath, op_depth,
                                           scratch_pool));

  /* ### TODO check for, and flag, tree conflict */
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

/* ### Drive TC_EDITOR so as to ...
 */
static svn_error_t *
update_moved_away_file(svn_editor_t *tc_editor,
                       svn_boolean_t add,
                       const char *src_relpath,
                       const char *dst_relpath,
                       const char *move_root_dst_relpath,
                       svn_revnum_t move_root_dst_revision,
                       svn_wc__db_t *db,
                       svn_wc__db_wcroot_t *wcroot,
                       apr_pool_t *scratch_pool)
{
  svn_kind_t kind;
  svn_stream_t *new_contents;
  const svn_checksum_t *new_checksum;
  apr_hash_t *new_props;

  /* Read post-update contents from the updated moved-away file and tell
   * the editor to merge them into the moved-here file. */
  SVN_ERR(svn_wc__db_read_pristine_info(NULL, &kind, NULL, NULL, NULL, NULL,
                                        &new_checksum, NULL, NULL, &new_props,
                                        db, svn_dirent_join(wcroot->abspath,
                                                            src_relpath,
                                                            scratch_pool),
                                        scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__db_pristine_read(&new_contents, NULL, db,
                                   wcroot->abspath, new_checksum,
                                   scratch_pool, scratch_pool));

  if (add)
    /* FIXME: editor API violation: missing svn_editor_alter_directory. */
    SVN_ERR(svn_editor_add_file(tc_editor, dst_relpath,
                                new_checksum, new_contents,
                                apr_hash_make(scratch_pool), /* ### TODO props */
                                move_root_dst_revision));
  else
    SVN_ERR(svn_editor_alter_file(tc_editor, dst_relpath,
                                  move_root_dst_revision,
                                  new_props, new_checksum,
                                  new_contents));

  SVN_ERR(svn_stream_close(new_contents));

  return SVN_NO_ERROR;
}

/* ### Drive TC_EDITOR so as to ...
 */
static svn_error_t *
update_moved_away_dir(svn_editor_t *tc_editor,
                      svn_boolean_t add,
                      apr_hash_t *children_hash,
                      const char *src_relpath,
                      const char *dst_relpath,
                      const char *move_root_dst_relpath,
                      svn_revnum_t move_root_dst_revision,
                      svn_wc__db_t *db,
                      svn_wc__db_wcroot_t *wcroot,
                      apr_pool_t *scratch_pool)
{
  apr_array_header_t *new_children;
  apr_hash_t *new_props;
  const char *src_abspath = svn_dirent_join(wcroot->abspath,
                                            src_relpath,
                                            scratch_pool);

  SVN_ERR(svn_hash_keys(&new_children, children_hash, scratch_pool));

  SVN_ERR(svn_wc__db_read_pristine_props(&new_props, db, src_abspath,
                                         scratch_pool, scratch_pool));

  if (add)
    SVN_ERR(svn_editor_add_directory(tc_editor, dst_relpath,
                                     new_children, new_props,
                                     move_root_dst_revision));
  else
    SVN_ERR(svn_editor_alter_directory(tc_editor, dst_relpath,
                                       move_root_dst_revision,
                                       new_children, new_props));

  return SVN_NO_ERROR;
}

/* ### Drive TC_EDITOR so as to ...
 */
static svn_error_t *
update_moved_away_subtree(svn_editor_t *tc_editor,
                          svn_boolean_t add,
                          const char *src_relpath,
                          const char *dst_relpath,
                          int src_op_depth,
                          const char *move_root_dst_relpath,
                          svn_revnum_t move_root_dst_revision,
                          svn_wc__db_t *db,
                          svn_wc__db_wcroot_t *wcroot,
                          apr_pool_t *scratch_pool)
{
  apr_hash_t *src_children, *dst_children;
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;

  SVN_ERR(svn_wc__db_get_children_op_depth(&src_children, wcroot,
                                           src_relpath, src_op_depth,
                                           scratch_pool, scratch_pool));

  SVN_ERR(update_moved_away_dir(tc_editor, add, src_children,
                                src_relpath, dst_relpath,
                                move_root_dst_relpath,
                                move_root_dst_revision,
                                db, wcroot, scratch_pool));

  SVN_ERR(svn_wc__db_get_children_op_depth(&dst_children, wcroot,
                                           dst_relpath,
                                           relpath_depth(move_root_dst_relpath),
                                           scratch_pool, scratch_pool));
  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, src_children);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *src_name = svn__apr_hash_index_key(hi);
      svn_kind_t *src_kind = svn__apr_hash_index_val(hi);
      svn_kind_t *dst_kind = apr_hash_get(dst_children, src_name,
                                          APR_HASH_KEY_STRING);
      svn_boolean_t is_add = FALSE;
      const char *child_src_relpath, *child_dst_relpath;

      svn_pool_clear(iterpool);

      child_src_relpath = svn_relpath_join(src_relpath, src_name, iterpool);
      child_dst_relpath = svn_relpath_join(dst_relpath, src_name, iterpool);

      if (!dst_kind || (*src_kind != *dst_kind))
        {
          is_add = TRUE;
          if (dst_kind)
            /* FIXME:editor API violation:missing svn_editor_alter_directory. */
            SVN_ERR(svn_editor_delete(tc_editor, child_dst_relpath,
                                      move_root_dst_revision));
        }

      if (*src_kind == svn_kind_file || *src_kind == svn_kind_symlink)
        {
          SVN_ERR(update_moved_away_file(tc_editor, is_add,
                                         child_src_relpath,
                                         child_dst_relpath,
                                         move_root_dst_relpath,
                                         move_root_dst_revision,
                                         db, wcroot, iterpool));
        }
      else if (*src_kind == svn_kind_dir)
        {
          SVN_ERR(update_moved_away_subtree(tc_editor, is_add,
                                            child_src_relpath,
                                            child_dst_relpath,
                                            src_op_depth,
                                            move_root_dst_relpath,
                                            move_root_dst_revision,
                                            db, wcroot, iterpool));
        }
      else
        ; /* ### TODO */

      apr_hash_set(dst_children, src_name, APR_HASH_KEY_STRING, NULL);
    }
  for (hi = apr_hash_first(scratch_pool, dst_children);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *dst_name = svn__apr_hash_index_key(hi);
      const char *child_dst_relpath;

      svn_pool_clear(iterpool);
      child_dst_relpath = svn_relpath_join(dst_relpath, dst_name, iterpool);

      /* FIXME: editor API violation: missing svn_editor_alter_directory. */
      SVN_ERR(svn_editor_delete(tc_editor, child_dst_relpath,
                                move_root_dst_revision));
    }
  svn_pool_destroy(iterpool);

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

  /* Replace entire subtree at one op-depth.

     ### FIXME: the delete/replace is destroying nested moves. */
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
  if (old_version->node_kind == svn_node_file)
    SVN_ERR(update_moved_away_file(tc_editor, FALSE, src_relpath, dst_relpath,
                                   dst_relpath, old_version->peg_rev,
                                   db, wcroot, scratch_pool));
  else if (old_version->node_kind == svn_node_dir)
    SVN_ERR(update_moved_away_subtree(tc_editor, FALSE,
                                      src_relpath, dst_relpath,
                                      src_op_depth,
                                      dst_relpath, old_version->peg_rev,
                                      db, wcroot, scratch_pool));

  SVN_ERR(svn_editor_complete(tc_editor));

  SVN_ERR(replace_moved_layer(src_relpath, dst_relpath, src_op_depth,
                              wcroot, scratch_pool));

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

static svn_error_t *
bump_moved_away(svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath,
                svn_depth_t depth,
                int op_depth,
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
                err = bump_moved_away(wcroot, dst_relpath, depth,
                                      relpath_depth(dst_relpath), iterpool);
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
  /* ### TODO: raise tree-conflicts? */
  if (depth != svn_depth_infinity)
    return SVN_NO_ERROR;

  SVN_ERR(bump_moved_away(wcroot, local_relpath, depth, 0, scratch_pool));

  return SVN_NO_ERROR;
}
