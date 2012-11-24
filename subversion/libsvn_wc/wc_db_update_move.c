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
 * An operation such as update can produce incoming changes for a
 * locally moved-away subtree, causing a tree-conflict to be flagged.
 * This editor transfers these changes from the moved-away part of the
 * working copy to the corresponding moved-here part of the working copy.
 *
 * Both the driver and receiver components of the editor are implemented
 * in this file.
 */

#define SVN_WC__I_AM_WC_DB

#include "svn_checksum.h"
#include "svn_dirent_uri.h"
#include "svn_editor.h"
#include "svn_error.h"
#include "svn_wc.h"
#include "svn_pools.h"

#include "private/svn_skel.h"
#include "private/svn_sqlite.h"
#include "private/svn_wc_private.h"

#include "wc.h"
#include "wc_db_private.h"
#include "conflicts.h"
#include "workqueue.h"

/*
 * Receiver code.
 */

struct tc_editor_baton {
  const char *src_relpath;
  const char *dst_relpath;
  svn_wc__db_t *db;
  svn_wc__db_wcroot_t *wcroot;
  svn_skel_t **work_items;
  svn_wc_conflict_version_t *old_version;
  svn_wc_conflict_version_t *new_version;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  apr_pool_t *result_pool;
};

static svn_error_t *
tc_editor_add_directory(void *baton,
                        const char *relpath,
                        const apr_array_header_t *children,
                        apr_hash_t *props,
                        svn_revnum_t replaces_rev,
                        apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
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
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
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

static svn_error_t *
tc_editor_alter_directory(void *baton,
                          const char *relpath,
                          svn_revnum_t revision,
                          const apr_array_header_t *children,
                          apr_hash_t *props,
                          apr_pool_t *scratch_pool)
{
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
}

static svn_error_t *
tc_editor_alter_file(void *baton,
                     const char *dst_relpath,
                     svn_revnum_t expected_moved_here_revision,
                     apr_hash_t *props,
                     const svn_checksum_t *moved_away_checksum,
                     svn_stream_t *post_update_contents,
                     apr_pool_t *scratch_pool)
{
  struct tc_editor_baton *b = baton;
  const svn_checksum_t *moved_here_checksum;
  const char *original_repos_relpath;
  svn_revnum_t original_revision;
  svn_kind_t kind;

  /* Get kind, revision, and checksum of the moved-here node. */
  /* 
   * ### Currently doesn't work right if the moved-away node has been replaced.
   * ### Need to read info from the move op-root's op-depth, not WORKING, to
   * ### properly update shadowed nodes within multi-layer move destinations.
   */
  SVN_ERR(svn_wc__db_read_info_internal(NULL, &kind, NULL, NULL, NULL, NULL,
                                        NULL, NULL, NULL, &moved_here_checksum,
                                        NULL, &original_repos_relpath, NULL,
                                        &original_revision, NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL, b->wcroot, dst_relpath,
                                        scratch_pool, scratch_pool));
  SVN_ERR_ASSERT(original_revision == expected_moved_here_revision);

  /* ### check original revision against moved-here op-root revision? */
  if (kind != svn_kind_file)
    return SVN_NO_ERROR;

  /* ### what if checksum kind differs?*/
  if (!svn_checksum_match(moved_away_checksum, moved_here_checksum))
    {
      const char *moved_to_abspath = svn_dirent_join(b->wcroot->abspath,
                                                     dst_relpath,
                                                     scratch_pool);
      const char *pre_update_pristine_abspath;
      const char *post_update_pristine_abspath;
      svn_skel_t *conflict_skel;
      enum svn_wc_merge_outcome_t merge_outcome;
      svn_wc_notify_state_t content_state;
      svn_wc_notify_t *notify;

      /*
       * Run a 3-way merge to update the file, using the pre-update
       * pristine text as the merge base, the post-update pristine
       * text as the merge-left version, and the current content of the
       * moved-here working file as the merge-right version.
       */
      SVN_ERR(svn_wc__db_pristine_get_path(&pre_update_pristine_abspath,
                                           b->db, moved_to_abspath,
                                           moved_here_checksum,
                                           scratch_pool, scratch_pool));
      SVN_ERR(svn_wc__db_pristine_get_path(&post_update_pristine_abspath,
                                           b->db, moved_to_abspath,
                                           moved_away_checksum,
                                           scratch_pool, scratch_pool));
      SVN_ERR(svn_wc__internal_merge(b->work_items, &conflict_skel,
                                     &merge_outcome, b->db,
                                     pre_update_pristine_abspath,
                                     post_update_pristine_abspath,
                                     moved_to_abspath,
                                     moved_to_abspath,
                                     NULL, NULL, NULL, /* diff labels */
                                     NULL, /* actual props */
                                     FALSE, /* dry-run */
                                     NULL, /* diff3-cmd */
                                     NULL, /* merge options */
                                     NULL, /* prop_diff */
                                     NULL, NULL, /* cancel_func + baton */
                                     b->result_pool, scratch_pool));

      if (merge_outcome == svn_wc_merge_conflict)
        {
          svn_skel_t *work_item;
          svn_wc_conflict_version_t *original_version;

          if (conflict_skel)
            {
              original_version = svn_wc_conflict_version_dup(b->old_version,
                                                             scratch_pool);
              original_version->path_in_repos = original_repos_relpath;
              original_version->node_kind = svn_node_file;
              SVN_ERR(svn_wc__conflict_skel_set_op_update(conflict_skel,
                                                          original_version,
                                                          scratch_pool,
                                                          scratch_pool));
              SVN_ERR(svn_wc__conflict_create_markers(&work_item, b->db,
                                                      moved_to_abspath,
                                                      conflict_skel,
                                                      scratch_pool,
                                                      scratch_pool));
              *b->work_items = svn_wc__wq_merge(*b->work_items, work_item,
                                                b->result_pool);
            }
          content_state = svn_wc_notify_state_conflicted;
        }
      else
        {
          svn_boolean_t is_locally_modified;

          SVN_ERR(svn_wc__internal_file_modified_p(&is_locally_modified,
                                                   b->db, moved_to_abspath,
                                                   FALSE /* exact_comparison */,
                                                   scratch_pool));
          if (is_locally_modified)
            content_state = svn_wc_notify_state_merged;
          else
            content_state = svn_wc_notify_state_changed;
        }

      notify = svn_wc_create_notify(moved_to_abspath,
                                    svn_wc_notify_update_update,
                                    scratch_pool);
      notify->kind = svn_node_file;
      notify->content_state = content_state;
      notify->prop_state = svn_wc_notify_state_unknown; /* ### TODO */
      notify->old_revision = b->old_version->peg_rev;
      notify->revision = b->new_version->peg_rev;
      b->notify_func(b->notify_baton, notify, scratch_pool);
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
  return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL, NULL);
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
  svn_wc_notify_t *notify;

  notify = svn_wc_create_notify(svn_dirent_join(b->wcroot->abspath,
                                                b->dst_relpath,
                                                scratch_pool),
                                svn_wc_notify_update_completed,
                                scratch_pool);
  notify->kind = svn_node_none;
  notify->content_state = svn_wc_notify_state_inapplicable;
  notify->prop_state = svn_wc_notify_state_inapplicable;
  notify->revision = b->new_version->peg_rev;
  b->notify_func(b->notify_baton, notify, scratch_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
tc_editor_abort(void *baton,
                apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

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
 */

static svn_error_t *
get_tc_info(svn_wc_operation_t *operation,
            svn_wc_conflict_reason_t *local_change,
            svn_wc_conflict_action_t *incoming_change,
            svn_wc_conflict_version_t **old_version,
            svn_wc_conflict_version_t **new_version,
            const char *src_abspath,
            svn_wc__db_t *db,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  const apr_array_header_t *locations;
  svn_boolean_t tree_conflicted;
  svn_skel_t *conflict_skel;
  svn_kind_t kind;

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
      *old_version = APR_ARRAY_IDX(locations, 0,
                                     svn_wc_conflict_version_t *);
      if (locations->nelts > 1)
        *new_version = APR_ARRAY_IDX(locations, 1,
                                     svn_wc_conflict_version_t *);
      else
        {
          const char *repos_root_url;
          const char *repos_uuid;
          const char *repos_relpath;
          svn_revnum_t revision;
          svn_node_kind_t node_kind;

          /* Construct b->new_version from BASE info. */
          SVN_ERR(svn_wc__db_base_get_info(NULL, &kind, &revision,
                                           &repos_relpath, &repos_root_url,
                                           &repos_uuid, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL, NULL,
                                           db, src_abspath, result_pool,
                                           scratch_pool));
          node_kind = svn__node_kind_from_kind(kind);
          *new_version = svn_wc_conflict_version_create2(repos_root_url,
                                                         repos_uuid,
                                                         repos_relpath,
                                                         revision,
                                                         node_kind,
                                                         scratch_pool);
        }
    }

  SVN_ERR(svn_wc__conflict_read_tree_conflict(local_change,
                                              incoming_change,
                                              db, src_abspath,
                                              conflict_skel, scratch_pool,
                                              scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
update_moved_away_file(svn_editor_t *tc_editor,
                       const char *src_relpath,
                       const char *dst_relpath,
                       const char *move_dst_op_root_relpath,
                       svn_revnum_t move_dst_op_root_revision,
                       svn_wc__db_t *db,
                       svn_wc__db_wcroot_t *wcroot,
                       apr_pool_t *scratch_pool)
{
  svn_kind_t kind;
  svn_stream_t *post_update_contents;
  const svn_checksum_t *moved_away_checksum;
                    
  /* Read post-update contents from the updated moved-away file and tell
   * the editor to merge them into the moved-here file. */
  SVN_ERR(svn_wc__db_read_pristine_info(NULL, &kind, NULL, NULL, NULL, NULL,
                                        &moved_away_checksum, NULL, NULL, db,
                                        svn_dirent_join(wcroot->abspath,
                                                        src_relpath,
                                                        scratch_pool),
                                        scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__db_pristine_read(&post_update_contents, NULL, db,
                                   wcroot->abspath, moved_away_checksum,
                                   scratch_pool, scratch_pool));
  SVN_ERR(svn_editor_alter_file(tc_editor, dst_relpath,
                                move_dst_op_root_revision,
                                NULL, /* ### TODO props */
                                moved_away_checksum,
                                post_update_contents));
  SVN_ERR(svn_stream_close(post_update_contents));

  return SVN_NO_ERROR;
}

static svn_error_t *
update_moved_away_dir(svn_editor_t *tc_editor,
                      const char *src_relpath,
                      const char *dst_relpath,
                      const char *move_dst_op_root_relpath,
                      svn_revnum_t move_dst_op_root_revision,
                      svn_wc__db_t *db,
                      svn_wc__db_wcroot_t *wcroot,
                      apr_pool_t *scratch_pool)
{
  /* ### notify */

  /* ### update prop content if changed */

  /* ### update list of children if changed */

  return SVN_NO_ERROR;
}

static svn_error_t *
update_moved_away_subtree(svn_editor_t *tc_editor,
                          const char *src_relpath,
                          const char *dst_relpath,
                          const char *move_dst_op_root_relpath,
                          svn_revnum_t move_dst_op_root_revision,
                          svn_wc__db_t *db,
                          svn_wc__db_wcroot_t *wcroot,
                          apr_pool_t *scratch_pool)
{
  const apr_array_header_t *children;
  apr_pool_t *iterpool;
  int i;

  SVN_ERR(update_moved_away_dir(tc_editor, src_relpath, dst_relpath,
                                move_dst_op_root_relpath,
                                move_dst_op_root_revision,
                                db, wcroot, scratch_pool));

  SVN_ERR(svn_wc__db_base_get_children(&children, db,
                                       svn_dirent_join(wcroot->abspath,
                                                       src_relpath,
                                                       scratch_pool),
                                       scratch_pool, scratch_pool));
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < children->nelts; i++)
    {
      const char *child_src_relpath;
      svn_kind_t child_kind;
      const char *child_dst_op_root_relpath;
      const char *child_dst_relpath;

      svn_pool_clear(iterpool);

      child_src_relpath = svn_relpath_join(src_relpath,
                                           APR_ARRAY_IDX(children, i,
                                                         const char *),
                                           iterpool);

      /* Is this child part of our move operation? */
      /* ### need to handle children which were newly added or removed
       * ### during the update */
      SVN_ERR(svn_wc__db_scan_deletion_internal(NULL, &child_dst_relpath, NULL,
                                                &child_dst_op_root_relpath,
                                                wcroot, child_src_relpath,
                                                iterpool, iterpool));
      if (child_dst_op_root_relpath == NULL ||
          strcmp(child_dst_op_root_relpath, move_dst_op_root_relpath) != 0)
        continue;

      SVN_ERR(svn_wc__db_base_get_info_internal(NULL, &child_kind,
                                                NULL, NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL,
                                                wcroot, child_src_relpath,
                                                iterpool, iterpool));

      if (child_kind == svn_kind_file || child_kind == svn_kind_symlink)
        SVN_ERR(update_moved_away_file(tc_editor, child_src_relpath,
                                       child_dst_relpath,
                                       move_dst_op_root_relpath,
                                       move_dst_op_root_revision,
                                       db, wcroot, iterpool));
      else if (child_kind == svn_kind_dir)
        SVN_ERR(update_moved_away_subtree(tc_editor, child_src_relpath,
                                          child_dst_relpath,
                                          move_dst_op_root_relpath,
                                          move_dst_op_root_revision,
                                          db, wcroot, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
drive_tree_conflict_editor(svn_editor_t *tc_editor,
                           const char *src_relpath,
                           const char *dst_relpath,
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

  /*
   * Drive the TC editor to transfer incoming changes from the move source
   * to the move destination.
   *
   * The pre-update tree is within dst at the op-depth of the move's op-root.
   * The post-update base tree is within src at op-depth zero.
   *
   * We walk the move source (i.e. the post-update tree), comparing each node
   * with the equivalent node at the move destination and applying the update
   * to nodes at the move destination.
   */
  if (old_version->node_kind == svn_node_file)
    SVN_ERR(update_moved_away_file(tc_editor, src_relpath, dst_relpath,
                                   dst_relpath, old_version->peg_rev,
                                   db, wcroot, scratch_pool));
  else if (old_version->node_kind == svn_node_dir)
    SVN_ERR(update_moved_away_subtree(tc_editor, src_relpath, dst_relpath,
                                      dst_relpath, old_version->peg_rev,
                                      db, wcroot, scratch_pool));

  SVN_ERR(svn_editor_complete(tc_editor));

  return SVN_NO_ERROR;
}

struct update_moved_away_conflict_victim_baton {
  svn_skel_t **work_items;
  const char *victim_relpath;
  svn_wc_operation_t operation;
  svn_wc_conflict_reason_t local_change;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_version_t *old_version;
  svn_wc_conflict_version_t *new_version;
  svn_wc__db_t *db;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
  apr_pool_t *result_pool;
};

/* An implementation of svn_wc__db_txn_callback_t. */
static svn_error_t *
update_moved_away_conflict_victim(void *baton,
                                  svn_wc__db_wcroot_t *wcroot,
                                  const char *victim_relpath,
                                  apr_pool_t *scratch_pool)
{
  struct update_moved_away_conflict_victim_baton *b = baton;
  svn_editor_t *tc_editor;
  struct tc_editor_baton *tc_editor_baton;

  /* ### assumes wc write lock already held */

  /* Construct editor baton. */
  tc_editor_baton = apr_pcalloc(scratch_pool, sizeof(*tc_editor_baton));
  tc_editor_baton->src_relpath = b->victim_relpath;
  SVN_ERR(svn_wc__db_scan_deletion_internal(NULL,
                                            &tc_editor_baton->dst_relpath,
                                            NULL, NULL, wcroot,
                                            tc_editor_baton->src_relpath,
                                            scratch_pool, scratch_pool));
  if (tc_editor_baton->dst_relpath == NULL)
    return svn_error_createf(SVN_ERR_WC_CONFLICT_RESOLVER_FAILURE, NULL,
                             _("The node '%s' has not been moved away"),
                             svn_dirent_local_style(
                               svn_dirent_join(wcroot->abspath,
                                               b->victim_relpath,
                                               scratch_pool),
                               scratch_pool));
  tc_editor_baton->old_version= b->old_version;
  tc_editor_baton->new_version= b->new_version;
  tc_editor_baton->db = b->db;
  tc_editor_baton->wcroot = wcroot;
  tc_editor_baton->work_items = b->work_items;
  tc_editor_baton->notify_func = b->notify_func;
  tc_editor_baton->notify_baton = b->notify_baton;
  tc_editor_baton->result_pool = b->result_pool;

  /* Create the editor... */
  SVN_ERR(svn_editor_create(&tc_editor, tc_editor_baton,
                            b->cancel_func, b->cancel_baton,
                            scratch_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(tc_editor, &editor_ops, scratch_pool));

  /* ... and drive it. */
  SVN_ERR(drive_tree_conflict_editor(tc_editor,
                                     tc_editor_baton->src_relpath,
                                     tc_editor_baton->dst_relpath,
                                     b->operation,
                                     b->local_change, b->incoming_change,
                                     tc_editor_baton->old_version,
                                     tc_editor_baton->new_version,
                                     b->db, wcroot,
                                     b->cancel_func, b->cancel_baton,
                                     scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_update_moved_away_conflict_victim(svn_skel_t **work_items,
                                             const char *victim_abspath,
                                             svn_wc__db_t *db,
                                             svn_wc_notify_func2_t notify_func,
                                             void *notify_baton,
                                             svn_cancel_func_t cancel_func,
                                             void *cancel_baton,
                                             apr_pool_t *result_pool,
                                             apr_pool_t *scratch_pool)
{
  struct update_moved_away_conflict_victim_baton b;
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_wc_operation_t operation;
  svn_wc_conflict_reason_t local_change;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_version_t *old_version;
  svn_wc_conflict_version_t *new_version;

  SVN_ERR(get_tc_info(&operation, &local_change, &incoming_change,
                      &old_version, &new_version,
                      victim_abspath, db, scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, victim_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  b.work_items = work_items;
  b.victim_relpath = local_relpath;
  b.operation = operation;
  b.local_change = local_change;
  b.incoming_change = incoming_change;
  b.old_version = old_version;
  b.new_version = new_version;
  b.db = db;
  b.notify_func = notify_func;
  b.notify_baton = notify_baton;
  b.cancel_func = cancel_func;
  b.cancel_baton = cancel_baton;
  b.result_pool = result_pool;

  SVN_ERR(svn_wc__db_with_txn(wcroot, local_relpath,
                              update_moved_away_conflict_victim, &b,
                              scratch_pool));

  return SVN_NO_ERROR;
}
