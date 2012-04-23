/*
 * editor.c:  Editor for modifying FS transactions
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
#include "svn_fs.h"

#include "fs-loader.h"


struct edit_baton {
  svn_fs_txn_t *txn;
  svn_boolean_t no_autocommit;
};


static svn_error_t *
make_editor(svn_editor_t **editor,
            svn_fs_txn_t *txn,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  static const svn_editor_cb_many_t editor_cbs = {
    NULL /* add_directory_cb */,
    NULL /* add_file_cb */,
    NULL /* add_symlink_cb */,
    NULL /* add_absent_cb */,
    NULL /* alter_directory_cb */,
    NULL /* alter_file_cb */,
    NULL /* alter_symlink_cb */,
    NULL /* delete_cb */,
    NULL /* copy_cb */,
    NULL /* move_cb */,
    NULL /* rotate_cb */,
    NULL /* complete_cb */,
    NULL /* abort_cb */
  };
  struct edit_baton *eb = apr_palloc(result_pool, sizeof(*eb));

  eb->txn = txn;
  eb->no_autocommit = (flags & SVN_FS_TXN_NO_AUTOCOMMIT) != 0;

  SVN_ERR(svn_editor_create(editor, eb, cancel_func, cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(*editor, &editor_cbs, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_editor_create(svn_editor_t **editor,
                     const char **txn_name,
                     svn_fs_t *fs,
                     svn_revnum_t revision,
                     apr_uint32_t flags,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_fs_txn_t *txn;

  SVN_ERR(svn_fs_begin_txn2(&txn, fs, revision, flags, result_pool));
  SVN_ERR(svn_fs_txn_name(txn_name, txn, result_pool));
  return svn_error_trace(make_editor(editor, txn,
                                     cancel_func, cancel_baton,
                                     result_pool, scratch_pool));
}


svn_error_t *
svn_fs_editor_create_for(svn_editor_t **editor,
                         svn_fs_t *fs,
                         const char *txn_name,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_fs_txn_t *txn;

  SVN_ERR(svn_fs_open_txn(&txn, fs, txn_name, result_pool));
  return svn_error_trace(make_editor(editor, txn,
                                     cancel_func, cancel_baton,
                                     result_pool, scratch_pool));
}

