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

#include "svn_private_config.h"

#include "fs-loader.h"


struct edit_baton {
  /* The transaction associated with this editor.  */
  svn_fs_txn_t *txn;

  /* Should the transaction be committed when complete_cb() is invoked?  */
  svn_boolean_t autocommit;

  /* We sometimes need the cancellation beyond what svn_editor_t provides  */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* The pool that the txn lives within. When we create a ROOT, it will
     be allocated within a subpool of this. The root will be closed in
     complete/abort and that subpool will be destroyed.

     This pool SHOULD NOT be used for any allocations.  */
  apr_pool_t *txn_pool;

  /* This is the root from the txn. Use get_root() to fetch/create this
     member as appropriate.  */
  svn_fs_root_t *root;
};

#define FSPATH(relpath, pool) apr_pstrcat(pool, "/", relpath, NULL)
#define UNUSED(x) ((void)(x))


static svn_error_t *
get_root(svn_fs_root_t **root,
         struct edit_baton *eb)
{
  if (eb->root == NULL)
    SVN_ERR(svn_fs_txn_root(&eb->root, eb->txn, eb->txn_pool));
  *root = eb->root;
  return SVN_NO_ERROR;
}


/* Apply each property in PROPS to the node at FSPATH in ROOT.  */
static svn_error_t *
add_new_props(svn_fs_root_t *root,
              const char *fspath,
              apr_hash_t *props,
              apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  /* ### it would be nice to have svn_fs_set_node_props(). but since we
     ### don't... add each property to the node. this is a new node, so
     ### we don't need to worry about deleting props. just adding.  */

  for (hi = apr_hash_first(scratch_pool, props); hi;
       hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      const svn_string_t *value = svn__apr_hash_index_val(hi);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_fs_change_node_prop(root, fspath, name, value, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


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
  const char *fspath = FSPATH(relpath, scratch_pool);
  svn_fs_root_t *root;

  /* Note: we ignore CHILDREN. We have no "incomplete" state to worry about,
     so we don't need to be aware of what children will be created.  */

  SVN_ERR(get_root(&root, eb));

  /* ### validate REPLACES_REV  */

  SVN_ERR(svn_fs_make_dir(root, fspath, scratch_pool));
  SVN_ERR(add_new_props(root, fspath, props, scratch_pool));

  return SVN_NO_ERROR;
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
  const char *fspath = FSPATH(relpath, scratch_pool);
  svn_fs_root_t *root;
  svn_stream_t *fs_contents;

  SVN_ERR(get_root(&root, eb));

  /* ### do something with CHECKSUM  */
  /* ### validate REPLACES_REV  */

  SVN_ERR(svn_fs_make_file(root, fspath, scratch_pool));

  /* ### We probably don't have an MD5 checksum, so no digest is available
     ### for svn_fs_apply_text() to validate. It would be nice to have an
     ### FS API that takes our CONTENTS/CHECKSUM pair (and PROPS!).  */
  SVN_ERR(svn_fs_apply_text(&fs_contents, root, fspath,
                            NULL /* result_checksum */,
                            scratch_pool));
  SVN_ERR(svn_stream_copy3(contents, fs_contents,
                           eb->cancel_func, eb->cancel_baton,
                           scratch_pool));

  SVN_ERR(add_new_props(root, fspath, props, scratch_pool));

  return SVN_NO_ERROR;
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
  const char *fspath = FSPATH(relpath, scratch_pool);
  svn_fs_root_t *root;
  svn_stream_t *fs_contents;

  SVN_ERR(get_root(&root, eb));

  /* ### validate REPLACES_REV  */

  /* ### we probably need to construct a file with specific contents
     ### (until the FS grows some symlink APIs)  */
#if 0
  SVN_ERR(svn_fs_make_file(root, fspath, scratch_pool));
  SVN_ERR(svn_fs_apply_text(&fs_contents, root, fspath,
                            NULL /* result_checksum */,
                            scratch_pool));
  /* ### SVN_ERR(svn_stream_printf(fs_contents, ..., scratch_pool));  */

  SVN_ERR(add_new_props(root, fspath, props, scratch_pool));
#endif
  UNUSED(fspath); UNUSED(fs_contents);

  SVN__NOT_IMPLEMENTED();
}


/* This implements svn_editor_cb_add_absent_t */
static svn_error_t *
add_absent_cb(void *baton,
              const char *relpath,
              svn_kind_t kind,
              svn_revnum_t replaces_rev,
              apr_pool_t *scratch_pool)
{
  /* This is a programming error. Code should not attempt to create these
     kinds of nodes within the FS.  */
  return svn_error_create(
           SVN_ERR_UNSUPPORTED_FEATURE, NULL,
           N_("The filesystem does not support 'absent' nodes"));
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

  if (eb->root != NULL)
    {
      svn_fs_close_root(eb->root);
      eb->root = NULL;
    }

  /* ### check AUTOCOMMIT  */
  /* ### how to return the results of the commit? eg. revision  */

  return SVN_NO_ERROR;
}


/* This implements svn_editor_cb_abort_t */
static svn_error_t *
abort_cb(void *baton,
         apr_pool_t *scratch_pool)
{
  struct edit_baton *eb = baton;

  if (eb->root != NULL)
    {
      svn_fs_close_root(eb->root);
      eb->root = NULL;
    }

  /* ### should we examine the error and attempt svn_fs_purge_txn() ?  */
  SVN_ERR(svn_fs_abort_txn(eb->txn, scratch_pool));

  return SVN_NO_ERROR;
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
  struct edit_baton *eb = apr_pcalloc(result_pool, sizeof(*eb));

  eb->txn = txn;
  eb->autocommit = autocommit;
  eb->cancel_func = cancel_func;
  eb->cancel_baton = cancel_baton;
  eb->txn_pool = result_pool;

  SVN_ERR(svn_editor_create(editor, eb, cancel_func, cancel_baton,
                            result_pool, scratch_pool));
  SVN_ERR(svn_editor_setcb_many(*editor, &editor_cbs, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs_editor_create(svn_editor_t **editor,
                     const char **txn_name,
                     svn_fs_t *fs,
                     apr_uint32_t flags,
                     svn_boolean_t autocommit,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_revnum_t revision;
  svn_fs_txn_t *txn;

  SVN_ERR(svn_fs_youngest_rev(&revision, fs, scratch_pool));
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

