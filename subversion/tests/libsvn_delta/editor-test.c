/*
 * editor-test.c:  Test editor APIs
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

#include "../svn_test.h"

#include "svn_types.h"
#include "svn_error.h"
#include "svn_editor.h"

#include "../svn_test_fs.h"

/* We use svn_repos APIs in some of these tests simply for convenience. */

/* This implements svn_editor_cb_add_directory_t */
static svn_error_t *
add_directory_noop_cb(void *baton,
                      const char *relpath,
                      const apr_array_header_t *children,
                      apr_hash_t *props,
                      svn_revnum_t replaces_rev,
                      apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_add_file_t */
static svn_error_t *
add_file_noop_cb(void *baton,
                 const char *relpath,
                 apr_hash_t *props,
                 svn_revnum_t replaces_rev,
                 apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_add_symlink_t */
static svn_error_t *
add_symlink_noop_cb(void *baton,
                    const char *relpath,
                    const char *target,
                    apr_hash_t *props,
                    svn_revnum_t replaces_rev,
                    apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_add_absent_t */
static svn_error_t *
add_absent_noop_cb(void *baton,
                   const char *relpath,
                   svn_node_kind_t kind,
                   svn_revnum_t replaces_rev,
                   apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_set_props_t */
static svn_error_t *
set_props_noop_cb(void *baton,
                  const char *relpath,
                  svn_revnum_t revision,
                  apr_hash_t *props,
                  svn_boolean_t complete,
                  apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_set_text_t */
static svn_error_t *
set_text_noop_cb(void *baton,
                 const char *relpath,
                 svn_revnum_t revision,
                 const svn_checksum_t *checksum,
                 svn_stream_t *contents,
                 apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_set_target_t */
static svn_error_t *
set_target_noop_cb(void *baton,
                   const char *relpath,
                   svn_revnum_t revision,
                   const char *target,
                   apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_delete_t */
static svn_error_t *
delete_noop_cb(void *baton,
               const char *relpath,
               svn_revnum_t revision,
               apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_copy_t */
static svn_error_t *
copy_noop_cb(void *baton,
             const char *src_relpath,
             svn_revnum_t src_revision,
             const char *dst_relpath,
             svn_revnum_t replaces_rev,
             apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_move_t */
static svn_error_t *
move_noop_cb(void *baton,
             const char *src_relpath,
             svn_revnum_t src_revision,
             const char *dst_relpath,
             svn_revnum_t replaces_rev,
             apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_complete_t */
static svn_error_t *
complete_noop_cb(void *baton,
                 apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* This implements svn_editor_cb_abort_t */
static svn_error_t *
abort_noop_cb(void *baton,
              apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
get_noop_editor(svn_editor_t **editor,
                void *editor_baton,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_editor_cb_many_t editor_cbs = {
      add_directory_noop_cb,
      add_file_noop_cb,
      add_symlink_noop_cb,
      add_absent_noop_cb,
      set_props_noop_cb,
      set_text_noop_cb,
      set_target_noop_cb,
      delete_noop_cb,
      copy_noop_cb,
      move_noop_cb,
      complete_noop_cb,
      abort_noop_cb
    };

  SVN_ERR(svn_editor_create(editor, editor_baton, cancel_func, cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(*editor, &editor_cbs, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
editor_from_delta_editor_test(const svn_test_opts_t *opts,
                              apr_pool_t *pool)
{
  svn_repos_t *repos;
  svn_fs_t *fs;
  svn_fs_txn_t *txn;
  svn_fs_root_t *revision_root, *txn_root, *base_root;
  svn_revnum_t youngest_rev;
  void *dedit_baton;
  svn_delta_editor_t *deditor;
  svn_editor_t *editor;

  /* Create a filesystem and repository. */
  SVN_ERR(svn_test__create_repos(&repos, "ev2-from-delta-editor-test",
                                 opts, pool));
  fs = svn_repos_fs(repos);

  /* Prepare a txn to receive the greek tree. */
  SVN_ERR(svn_fs_begin_txn(&txn, fs, 0, pool));
  SVN_ERR(svn_fs_txn_root(&txn_root, txn, pool));

  /* Create and commit the greek tree. */
  SVN_ERR(svn_test__create_greek_tree(txn_root, pool));
  SVN_ERR(svn_repos_fs_commit_txn(NULL, repos, &youngest_rev, txn, pool));
  SVN_TEST_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));

  SVN_ERR(svn_fs_revision_root(&revision_root, fs, youngest_rev, pool));
  SVN_ERR(svn_fs_revision_root(&base_root, fs, 0, pool));

  /* Construct our editor, and from it a delta editor. */
  SVN_ERR(get_noop_editor(&editor, NULL, NULL, NULL, pool, pool));
  SVN_ERR(svn_delta_from_editor(&deditor, &dedit_baton, editor, pool));

  SVN_ERR(svn_repos_replay2(revision_root, "", SVN_INVALID_REVNUM, TRUE,
                            deditor, dedit_baton, NULL, NULL, pool));

  /* Close the edit. */
  SVN_ERR(deditor->close_edit(dedit_baton, pool));

  return SVN_NO_ERROR;
}



/* The test table.  */

struct svn_test_descriptor_t test_funcs[] =
  {
    SVN_TEST_NULL,
    SVN_TEST_OPTS_PASS(editor_from_delta_editor_test,
                       "editor creation from delta editor"),
    SVN_TEST_NULL
  };
