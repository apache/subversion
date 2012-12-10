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
#include "svn_props.h"
#include "svn_pools.h"

#include "private/svn_skel.h"
#include "private/svn_sqlite.h"
#include "private/svn_wc_private.h"

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
 * merge the edits into the 'actual' layer of a moved subtree.
 */

struct tc_editor_baton {
  svn_skel_t **work_items;
  svn_wc__db_t *db;
  svn_wc__db_wcroot_t *wcroot;
  const char *move_root_src_relpath;
  const char *move_root_dst_relpath;
  int src_op_depth;
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
  struct tc_editor_baton *b = baton;
  int op_depth = relpath_depth(b->move_root_dst_relpath);

  SVN_ERR(svn_wc__db_extend_parent_delete(b->wcroot, relpath, svn_kind_dir,
                                          op_depth, scratch_pool));

  /* ### TODO check for, and flag, tree conflict */

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

  SVN_ERR(svn_wc__db_extend_parent_delete(b->wcroot, relpath, svn_kind_file,
                                          op_depth, scratch_pool));

  /* ### TODO check for, and flag, tree conflict */

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


/* Check whether the node at LOCAL_RELPATH in the working copy at WCROOT
 * is shadowed by some node at a higher op depth than EXPECTED_OP_DEPTH. */
static svn_error_t *
check_shadowed_node(svn_boolean_t *is_shadowed,
                    int expected_op_depth,
                    const char *local_relpath,
                    svn_wc__db_wcroot_t *wcroot)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      int op_depth = svn_sqlite__column_int(stmt, 0);

      if (op_depth > expected_op_depth)
        {
          *is_shadowed = TRUE;
          SVN_ERR(svn_sqlite__reset(stmt));

          return SVN_NO_ERROR;
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  *is_shadowed = FALSE;
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

/* All the info we need about one version of a file node. */
typedef struct file_version_t
{
  svn_wc_conflict_version_t *location_and_kind;
  apr_hash_t *props;
  const svn_checksum_t *checksum;
} file_version_t;

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
 * Add any required work items to *WORK_ITEMS, allocated in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
update_working_file(svn_skel_t **work_items,
                    const char *local_relpath,
                    const char *repos_relpath,
                    const file_version_t *old_version,
                    const file_version_t *new_version,
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
  apr_hash_t *actual_props, *new_actual_props;
  apr_array_header_t *propchanges;
  enum svn_wc_merge_outcome_t merge_outcome;
  svn_wc_notify_state_t prop_state, content_state;

  /*
   * Run a 3-way prop merge to update the props, using the pre-update
   * props as the merge base, the post-update props as the
   * merge-left version, and the current props of the
   * moved-here working file as the merge-right version.
   */
  SVN_ERR(svn_wc__db_read_props(&actual_props,
                                db, local_abspath,
                                scratch_pool, scratch_pool));
  SVN_ERR(svn_prop_diffs(&propchanges,
                         new_version->props, old_version->props,
                         scratch_pool));
  SVN_ERR(svn_wc__merge_props(&conflict_skel, &prop_state,
                              NULL, &new_actual_props,
                              db, local_abspath,
                              old_version->props, old_version->props,
                              actual_props, propchanges,
                              scratch_pool, scratch_pool));
  /* ### TODO: Make a WQ item in WORK_ITEMS to set new_actual_props ... */
  /* ### Not a direct DB op like this... */
  SVN_ERR(svn_wc__db_op_set_props(db, local_abspath,
                                  new_actual_props,
                                  svn_wc__has_magic_property(propchanges),
                                  NULL/*conflict_skel*/, NULL/*work_items*/,
                                  scratch_pool));

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
      svn_skel_t *work_item;
      svn_wc_conflict_version_t *original_version;

      original_version = svn_wc_conflict_version_dup(
                           old_version->location_and_kind, scratch_pool);
      original_version->path_in_repos = repos_relpath;
      original_version->node_kind = svn_node_file;
      SVN_ERR(svn_wc__conflict_skel_set_op_update(conflict_skel,
                                                  original_version,
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
  file_version_t old_version, new_version;

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
      svn_boolean_t is_shadowed;

      /* If the node is shadowed by a higher layer, we need to flag a 
       * tree conflict and must not touch the working file. */
      SVN_ERR(check_shadowed_node(&is_shadowed,
                                  relpath_depth(b->move_root_dst_relpath),
                                  dst_relpath, b->wcroot));
      if (is_shadowed)
        {
          /* ### TODO flag tree conflict */
        }
      else
        SVN_ERR(update_working_file(b->work_items, dst_relpath,
                                    move_dst_repos_relpath,
                                    &old_version, &new_version,
                                    b->wcroot, b->db,
                                    b->notify_func, b->notify_baton,
                                    b->result_pool, scratch_pool));
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

/* An editor.
 *
 * This editor will merge the edits into the 'actual' tree at
 * MOVE_ROOT_DST_RELPATH (in struct tc_editor_baton),
 * perhaps raising conflicts if necessary.
 *
 */
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

          /* Construct new_version from BASE info. */
          SVN_ERR(svn_wc__db_base_get_info(NULL, &kind, &revision,
                                           &repos_relpath, &repos_root_url,
                                           &repos_uuid, NULL, NULL, NULL, NULL,
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
                      const char *src_relpath,
                      const char *dst_relpath,
                      const char *move_root_dst_relpath,
                      svn_revnum_t move_root_dst_revision,
                      svn_wc__db_t *db,
                      svn_wc__db_wcroot_t *wcroot,
                      apr_pool_t *scratch_pool)
{
  if (add)
    /* ### TODO children and props */
    SVN_ERR(svn_editor_add_directory(tc_editor, dst_relpath,
                                     apr_array_make(scratch_pool, 0,
                                                    sizeof (const char *)),
                                     apr_hash_make(scratch_pool),
                                     move_root_dst_revision));

  /* ### notify */

  /* ### update prop content if changed */

  /* ### update list of children if changed */

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

  SVN_ERR(update_moved_away_dir(tc_editor, add, src_relpath, dst_relpath,
                                move_root_dst_relpath,
                                move_root_dst_revision,
                                db, wcroot, scratch_pool));

  SVN_ERR(svn_wc__db_get_children_op_depth(&src_children, wcroot,
                                           src_relpath, src_op_depth,
                                           scratch_pool, scratch_pool));
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
                    svn_wc__db_t *db,
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
                              db, wcroot, scratch_pool));

  return SVN_NO_ERROR;
}

struct update_moved_away_conflict_victim_baton {
  svn_skel_t **work_items;
  svn_wc__db_t *db;
  svn_wc_operation_t operation;
  svn_wc_conflict_reason_t local_change;
  svn_wc_conflict_action_t incoming_change;
  svn_wc_conflict_version_t *old_version;
  svn_wc_conflict_version_t *new_version;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;
  apr_pool_t *result_pool;
};

/* The body of svn_wc__db_update_moved_away_conflict_victim(), which see.
 * An implementation of svn_wc__db_txn_callback_t. */
static svn_error_t *
update_moved_away_conflict_victim(void *baton,
                                  svn_wc__db_wcroot_t *wcroot,
                                  const char *victim_relpath,
                                  apr_pool_t *scratch_pool)
{
  struct update_moved_away_conflict_victim_baton *b = baton;
  svn_editor_t *tc_editor;
  struct tc_editor_baton *tc_editor_baton;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  /* ### assumes wc write lock already held */

  /* Construct editor baton. */
  tc_editor_baton = apr_pcalloc(scratch_pool, sizeof(*tc_editor_baton));
  tc_editor_baton->move_root_src_relpath = victim_relpath;
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
  tc_editor_baton->old_version= b->old_version;
  tc_editor_baton->new_version= b->new_version;
  tc_editor_baton->db = b->db;
  tc_editor_baton->wcroot = wcroot;
  tc_editor_baton->work_items = b->work_items;
  tc_editor_baton->notify_func = b->notify_func;
  tc_editor_baton->notify_baton = b->notify_baton;
  tc_editor_baton->result_pool = b->result_pool;

  /* ### TODO get from svn_wc__db_scan_deletion_internal? */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, victim_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    tc_editor_baton->src_op_depth = svn_sqlite__column_int(stmt, 0);
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
                            b->cancel_func, b->cancel_baton,
                            scratch_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(tc_editor, &editor_ops, scratch_pool));

  /* ... and drive it. */
  SVN_ERR(drive_tree_conflict_editor(tc_editor,
                                     tc_editor_baton->move_root_src_relpath,
                                     tc_editor_baton->move_root_dst_relpath,
                                     tc_editor_baton->src_op_depth,
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
                                             svn_wc__db_t *db,
                                             const char *victim_abspath,
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

  SVN_ERR(get_tc_info(&b.operation, &b.local_change, &b.incoming_change,
                      &b.old_version, &b.new_version,
                      db, victim_abspath,
                      scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                                db, victim_abspath,
                                                scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  b.work_items = work_items;
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
