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
  svn_boolean_t autocommit;
};

#define UNUSED(x) ((void)(x))


/* This implements svn_editor_cb_add_directory_t */
static svn_error_t *
add_directory_cb(void *baton,
                 const char *relpath,
                 const apr_array_header_t *children,
                 apr_hash_t *props,
                 svn_revnum_t replaces_rev,
                 apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_add_file_t */
static svn_error_t *
add_file_cb(void *baton,
            const char *relpath,
            const svn_checksum_t *checksum,
            svn_stream_t *contents,
            apr_hash_t *props,
            svn_revnum_t replaces_rev,
            apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_add_symlink_t */
static svn_error_t *
add_symlink_cb(void *baton,
               const char *relpath,
               const char *target,
               apr_hash_t *props,
               svn_revnum_t replaces_rev,
               apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_add_absent_t */
static svn_error_t *
add_absent_cb(void *baton,
              const char *relpath,
              svn_kind_t kind,
              svn_revnum_t replaces_rev,
              apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_alter_directory_t */
static svn_error_t *
alter_directory_cb(void *baton,
                   const char *relpath,
                   svn_revnum_t revision,
                   apr_hash_t *props,
                   apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_alter_file_t */
static svn_error_t *
alter_file_cb(void *baton,
              const char *relpath,
              svn_revnum_t revision,
              apr_hash_t *props,
              const svn_checksum_t *checksum,
              svn_stream_t *contents,
              apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_alter_symlink_t */
static svn_error_t *
alter_symlink_cb(void *baton,
                 const char *relpath,
                 svn_revnum_t revision,
                 apr_hash_t *props,
                 const char *target,
                 apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_delete_t */
static svn_error_t *
delete_cb(void *baton,
          const char *relpath,
          svn_revnum_t revision,
          apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_copy_t */
static svn_error_t *
copy_cb(void *baton,
        const char *src_relpath,
        svn_revnum_t src_revision,
        const char *dst_relpath,
        svn_revnum_t replaces_rev,
        apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_move_t */
static svn_error_t *
move_cb(void *baton,
        const char *src_relpath,
        svn_revnum_t src_revision,
        const char *dst_relpath,
        svn_revnum_t replaces_rev,
        apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_rotate_t */
static svn_error_t *
rotate_cb(void *baton,
          const apr_array_header_t *relpaths,
          const apr_array_header_t *revisions,
          apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_complete_t */
static svn_error_t *
complete_cb(void *baton,
            apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_abort_t */
static svn_error_t *
abort_cb(void *baton,
         apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  UNUSED(eb); SVN__NOT_IMPLEMENTED();
}


static svn_error_t *
make_editor(svn_editor_t **editor,
            svn_fs_txn_t *txn,
            svn_boolean_t autocommit,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  static const svn_editor_cb_many_t editor_cbs = {
    add_directory_cb,
    add_file_cb,
    add_symlink_cb,
    add_absent_cb,
    alter_directory_cb,
    alter_file_cb,
    alter_symlink_cb,
    delete_cb,
    copy_cb,
    move_cb,
    rotate_cb,
    complete_cb,
    abort_cb
  };
  struct edit_baton *eb = apr_palloc(result_pool, sizeof(*eb));

  eb->txn = txn;
  eb->autocommit = autocommit;

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
                     svn_boolean_t autocommit,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_fs_txn_t *txn;

  SVN_ERR(svn_fs_begin_txn2(&txn, fs, revision, flags, result_pool));
  SVN_ERR(svn_fs_txn_name(txn_name, txn, result_pool));
  return svn_error_trace(make_editor(editor, txn, autocommit,
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
  return svn_error_trace(make_editor(editor, txn, FALSE /* autocommit */,
                                     cancel_func, cancel_baton,
                                     result_pool, scratch_pool));
}

